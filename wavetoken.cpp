#include "wavetoken.hpp"
#include <eosio/system.hpp>
#include <eosio/transaction.hpp>
#include <eosio/crypto.hpp>

ACTION wavetoken::create( name issuer, asset maximum_supply )
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( maximum_supply.is_valid(), "invalid supply");
    check( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing == statstable.end(), "token with symbol already exists" );

    statstable.emplace( _self, [&]( auto& s ) {
       s.supply.symbol   = maximum_supply.symbol;
       s.max_supply      = maximum_supply;
       s.issuer          = issuer;
    });
}

ACTION wavetoken::issue( name to, asset quantity, string memo )
{
    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );

    auto sym_name = sym.code().raw();
    stats statstable( _self, sym_name );
    auto existing = statstable.find( sym_name );
    check( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    check( quantity.is_valid(), "invalid quantity." );
    check( quantity.amount > 0, "must issue positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    check( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify( st, _self, [&]( auto& s ) {
        s.supply += quantity;
    });

    add_balance(st.issuer, quantity, st, st.issuer);

    if( to != st.issuer ) {
        SEND_INLINE_ACTION( *this, transfer, {st.issuer,"active"_n}, {st.issuer, to, quantity, memo} );
    }
}

ACTION wavetoken::transfer( name from, name to, asset quantity, string memo )
{
    check( from != to, "cannot transfer to self" );
    require_auth( from );
    check( is_account( to ), "to account does not exist");

    auto sym = quantity.symbol.code().raw();
    stats statstable( _self, sym );
    const auto& st = statstable.get( sym );

    require_recipient( from );
    require_recipient( to );

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must transfer positive quantity" );
    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    // S9 아래에서 처리
    //sub_balance( from, quantity, st );
    //add_balance( to, quantity, st, from );
    // S9 - 전송 수수료 계산
    // 주의! - transfer, transferto, withdraw 에서 transfer를 호출하기 때문에 재귀 호출이 될 수 있어 체크가 필요.
    // deposit은 수수료를 떼지 않아야 하기에 받는 계정이 CONTRACT_ACCOUNT이면 받지 않음
    // to == FEE_ACCOUNT 를 체크하면 재귀호출이 되지 않음!
    // from: CONTRACT_ACCOUNT, EXCHANGE_ACCOUNT, FEE_ACCOUNT
    // to: CONTRACT_ACCOUNT, FEE_ACCOUNT
    if ((from != CONTRACT_ACCOUNT && from != EXCHANGE_ACCOUNT && from != FEE_ACCOUNT) && (to != CONTRACT_ACCOUNT && to != FEE_ACCOUNT))
    {
        asset fee_amount = asset(quantity.amount / TRANSFER_FEE_DIVISOR, quantity.symbol);
        asset transfer_amount = quantity - fee_amount;
        // 수수료 제외 부분을 제외하고 처리, 아래 SEND_INLINE_ACTION,transfer 에서 수수료를 from에서 제외하고 FEE_ACCOUNT에 보낸다.
        sub_balance( from, transfer_amount, quantity, st );
        add_balance( to, transfer_amount, st, from );
        // 수수료 계정으로 수수료 전송
        string memo="transfer fee";
        // if(fee_amount.amount > 0)
            SEND_INLINE_ACTION( *this, transfer, {from,"active"_n}, {from, FEE_ACCOUNT, fee_amount, memo} );
    }
    else
    {
        sub_balance( from, quantity, quantity, st );
        add_balance( to, quantity, st, from );
    }
}

// S9 lock 
ACTION wavetoken::lock(name user, asset quantity)
{
    require_auth(_self);
    require_recipient(user);

    accounts accounts_t(_self, user.value);
    auto user_balance = accounts_t.find(quantity.symbol.code().raw());
    check(user_balance != accounts_t.end(), "wavetoken : token is not exists");
    check(user_balance->balance.symbol == quantity.symbol, "symbol precision mismatch" );
    //check(user_balance->balance.amount >= quantity.amount, "insuffient quantity");

    accounts_locked accounts_locked_t(_self, user.value);
    auto lock = accounts_locked_t.find(quantity.symbol.code().raw());
    if(lock == accounts_locked_t.end())
    {
        check(user_balance->balance.amount >= quantity.amount, "insuffient quantity");
        accounts_locked_t.emplace(_self, [&](auto &u){
            u.balance = quantity;
        });
    }
    else
    {
        asset lock_amount = lock->balance + quantity;
        check(user_balance->balance.amount >= lock_amount.amount, "insuffient quantity");
        accounts_locked_t.modify(lock, _self, [&](auto &u){
            u.balance += quantity;
        });
    }
}    

// S9 unlock 
ACTION wavetoken::unlock(name user, asset quantity)
{
    require_auth(_self);
    require_recipient(user);

    accounts_locked accounts_locked_t(_self, user.value);
    auto lock = accounts_locked_t.find(quantity.symbol.code().raw());
    check(lock != accounts_locked_t.end(), "not exist locked token");
    check(lock->balance.amount >= quantity.amount, "invalid unlock quantity");

    if(lock->balance.amount == quantity.amount)
    {
        accounts_locked_t.erase(lock);
    }
    else
    {
        accounts_locked_t.modify(lock, _self, [&](auto &u){
            u.balance -= quantity;
        });
    }
}     

ACTION wavetoken::retire(name from, asset quantity, string memo)
{
    require_auth(from);

    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );

    auto sym_name = sym.code().raw();
    stats statstable_t( _self, sym_name );
    auto existing = statstable_t.find( sym_name );
    check( existing != statstable_t.end(), "token with symbol does not exist, create token before burn" );
    const auto& st = *existing;

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must burn positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    check( quantity.amount <= st.supply.amount, "quantity exceeds available supply");

    sub_balance( from, quantity, quantity, st );

    statstable_t.modify( st, _self, [&]( auto& s ) {
        s.supply -= quantity;
        //s.max_supply -= quantity;
    });

    sub_balance( _self, quantity, quantity, st );        
}

// S9 - total_value 추가
// transfer fee 가 나중에 전송되기 때문에 처음 전송할 때 fee포함된 금액, lock 때문에 추가
// fee 포함해서 lock free한 금액보다 많은 확인한다.
void wavetoken::sub_balance( name owner, asset value, asset total_value, const currency_stat& st ) {
    accounts from_acnts( _self, owner.value );

    const auto& from = from_acnts.get( value.symbol.code().raw() );
    check( from.balance.amount >= value.amount, "overdrawn balance" );

    //check locked balance
    accounts_locked accounts_locked_t(_self, owner.value);
    auto lock = accounts_locked_t.find(value.symbol.code().raw());
    if(lock != accounts_locked_t.end())
    {
        check( from.balance.amount - lock->balance.amount >= total_value.amount, "over transferable balance because of some balance is locked.");
    }

    // if( from.balance.amount == value.amount ) {
    //       from_acnts.erase( from );
    // }
    // else 
    {
        from_acnts.modify( from, owner, [&]( auto& a ) {
            a.balance -= value;
        });
    }
    auto global = global_t.find(0);
    if(global != global_t.end() && owner == _self)
    {
        global_t.modify(global, _self, [&](auto &g){
            g.balance -= value;
        });
    }    
}

void wavetoken::add_balance( name owner, asset value, const currency_stat& st, name ram_payer )
{
    accounts to_acnts( _self, owner.value );
    auto to = to_acnts.find( value.symbol.code().raw() );
    if( to == to_acnts.end() ) {
        to_acnts.emplace( ram_payer, [&]( auto& a ){
            a.balance = value;
        });
    } else {
        to_acnts.modify( to, _self, [&]( auto& a ) {
            a.balance += value;
        });
    }

    auto global = global_t.find(0);
    if(global != global_t.end() && owner == _self)
    {
        global_t.modify(global, _self, [&](auto &g){
            g.balance += value;
        });
    }    
}


inline checksum256 wavetoken::get_seed_from_tx()
{
    auto len = read_transaction(nullptr, 0);
    char *tx = (char*)malloc(len);
    read_transaction(tx,len);

    checksum256 seed;

    seed = sha256(tx, len);

    return seed;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// product

ACTION wavetoken::initcontract(uint64_t withdrawal_fee, uint64_t referral_fee, symbol sym)
{
    require_auth(_self);

    auto sym_name = sym.code().raw();
    stats statstable( _self, sym_name );
    const auto& st = statstable.get( sym_name );
    accounts accounts( _self, _self.value );
    auto to = accounts.find( sym_name );

    auto global = global_t.find(0);    
    if(global == global_t.end())
    {
        global_t.emplace(_self, [&](auto &g){
            g.id = 0;
            g.withdrawal_fee = withdrawal_fee;
            g.referral_fee = referral_fee;
            g.balance = to->balance;
            g.referral_amount = asset(0, sym);
            g.interest_amount = asset(0, sym);
            g.withdraw_amount = asset(0, sym);
            g.lockup_amount = asset(0, sym);
            g.liabilities_amount = asset(0, sym);
        });
    }
    else
    {
        global_t.modify(global, _self, [&](auto &g){
            g.balance = to->balance;
            g.withdrawal_fee = withdrawal_fee;
            g.referral_fee = referral_fee;
        });
    }

    auto user = users_t.find(_self.value);
    if(user == users_t.end())
    {
        users_t.emplace(_self, [&](auto &u){
            u.user = _self;
            u.balance = asset(0, sym);
            u.referral = _self;
        });
    }

}
void wavetoken::initglobalva(int mode)
{
    require_auth(_self);
    auto global = global_t.find(0);
    if(global != global_t.end())
    {
      switch(mode)
      {
          case 0:
            global_t.modify(global, _self, [&](auto &g){
                g.lockup_amount = asset(0, g.withdraw_amount.symbol);
                g.referral_amount = asset(0, g.withdraw_amount.symbol);
                g.withdraw_amount = asset(0, g.withdraw_amount.symbol);
                g.interest_amount = asset(0, g.withdraw_amount.symbol);
                g.liabilities_amount = asset(0, g.withdraw_amount.symbol);
            });            
            break;
      }
    }
/*
    do{
        auto p_users = p_user_t.begin();
        if(p_users == p_user_t.end()) break;
        p_user_t.erase( p_users );
    }while(true);    
    */
}
void wavetoken::add_log(int type, name user, asset quantity, string memo)
{
    auto counter = counter_t.find(LOG_COUNTER.value);
    if(counter == counter_t.end())
    {
       counter = counter_t.emplace(_self, [&](auto &c){
          c.key = LOG_COUNTER;
          c.counter = 0;
       });
    }
    else
    {
        counter_t.modify(counter, _self, [&](auto &c){
            c.counter++;
        });
    }
  
    log_t.emplace(_self, [&](auto &l){
        l.id = counter->counter;
        l.type = type;
        l.user = user;
        l.quantity = quantity;
        l.memo = memo;
        l.tx_id = get_seed_from_tx();
        l.created_at = eosio::current_time_point().sec_since_epoch();
    });  
}

ACTION wavetoken::adduser(name user){
    require_auth(_self);

    auto global = global_t.find(0);
    check(global != global_t.end(), "not initialized contract.");

    auto user_info = users_t.find(user.value);

    check(user_info == users_t.end(), "already exists user.");
    users_t.emplace(_self, [&](auto &u){
        u.user = user;
        u.balance = asset(0, global->balance.symbol);
        u.referral = _self;
    });
}
ACTION wavetoken::deposit(name from, asset quantity, name referral)
{
    require_auth(from);
    
    auto global = global_t.find(0);
    check(global != global_t.end(), "not initialized contract.");

    auto from_info = users_t.find(from.value);
    if(from_info == users_t.end())
    {
        if(referral == from) referral = _self;
        check(is_account(referral), "invalid referral");
        from_info = users_t.emplace(_self, [&](auto &u){
            u.user = from;
            u.balance = quantity;
            u.referral = referral;
        });
    }
    else
    {
        users_t.modify(from_info, _self, [&](auto &u){
            u.balance += quantity;
        });
        referral = from_info->referral;
    }
    
    string memo = referral.to_string();
    if(from != _self)
        SEND_INLINE_ACTION( *this, transfer, {from,"active"_n}, {from, _self, quantity, memo} );

    add_log(log_type::deposit, from, quantity, referral.to_string());

    if(referral == from || is_account(referral) == false)
    {
        global_t.modify(global, _self, [&](auto &g){
            g.liabilities_amount += quantity;
        });

        return;
    } 
    
    auto referral_fee = asset(quantity.amount * global->referral_fee / 1'0000, quantity.symbol);
    check(global->balance >= referral_fee, "insufficient global balance.");
    
    auto ref = users_t.find(from_info->referral.value);
    check(ref != users_t.end(), "invalid referral");
    users_t.modify(ref, _self, [&](auto &u){
        u.balance += referral_fee;
    });
    
    global_t.modify(global, _self, [&](auto &g){
        g.referral_amount += referral_fee;
        g.liabilities_amount += referral_fee + quantity;
    });
    
    memo = "referral";
    if(referral != _self)
        SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {referral, referral_fee, memo} );
}

ACTION wavetoken::referralto(name to, asset quantity)
{
    require_auth(_self);
    check( is_account( to ), "to account does not exist");
    
    auto global = global_t.find(0);
    check(global != global_t.end(), "not initialized contract.");

    auto to_info = users_t.find(to.value);
    if(to_info == users_t.end())
    {
        name referral = _self;
        check(is_account(referral), "invalid referral");
        to_info = users_t.emplace(_self, [&](auto &u){
            u.user = to;
            u.balance = quantity;
            u.referral = referral;
        });
    }
    else
    {
        users_t.modify(to_info, _self, [&](auto &u){
            u.balance += quantity;
        });
    }
    
    string memo = to.to_string();
    //if(from != _self)
    //    SEND_INLINE_ACTION( *this, transfer, {from,"active"_n}, {from, _self, quantity, memo_ex} );

    add_log(log_type::referral_to, _self, quantity, memo);

    global_t.modify(global, _self, [&](auto &g){
        g.referral_amount += quantity;
        g.liabilities_amount += quantity;
    });
}


ACTION wavetoken::withdraw(name user, asset quantity)
{
    require_auth(user);
    
    auto user_info = users_t.find(user.value);
    check(user_info != users_t.end(), "not exists user.");
    check(user_info->balance >= quantity, "insufficient balance.");
    
    auto global = global_t.find(0);
    check(global != global_t.end(), "not initialized contract.");
    asset fee_amount = asset(quantity.amount * global->withdrawal_fee / 1'0000, quantity.symbol);
    asset withdraw_amount = quantity - fee_amount;
    
    string memo="withdraw";
    SEND_INLINE_ACTION( *this, transfer, {_self,"active"_n}, {_self, user, withdraw_amount, memo} );
    memo="withdraw fee";
    // SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {_self, fee_amount, memo} );
    // S9 - 수수료를 정해진 계정에 전송 
    SEND_INLINE_ACTION( *this, transfer, {_self,"active"_n}, {_self, FEE_ACCOUNT, fee_amount, memo} );
    
    users_t.modify(user_info, _self, [&](auto &u){
        u.balance -= quantity;
    });
    
    global_t.modify(global, _self, [&](auto &g){
        //g.balance += fee_amount;
        g.withdraw_amount += fee_amount;
        g.liabilities_amount -= quantity;
    });

    user_info = users_t.find(_self.value);
    if(user_info != users_t.end())
    {
        users_t.modify(user_info, _self, [&](auto &u){
            u.balance += fee_amount;
        });
    }
    
    add_log(log_type::withdraw, user, quantity, "");
}

ACTION wavetoken::transferto(name from, name to, asset quantity)
{
    require_auth(from);
    auto from_info = users_t.find(from.value);
    check(from_info != users_t.end(), "not exists user(from).");
    check(from_info->balance >= quantity, "insufficient balance.");


    auto to_info = users_t.find(to.value);
    check(to_info != users_t.end(), "not exists user(to).");

    // S9 - 전송 수수료 계산 - 10%
    asset fee_amount = asset(quantity.amount / TRANSFER_TO_FEE_DIVISOR, quantity.symbol);
    asset transfer_amount = quantity - fee_amount;

    users_t.modify(from_info, _self, [&](auto &f){
        f.balance -= quantity;
    });   
    
    users_t.modify(to_info, _self, [&](auto &t){
        // 받는 사람은 수수료 제외된 금액을 받음
        //t.balance += quantity;
        t.balance += transfer_amount;
    });   

    require_recipient( from );
    require_recipient( to );
 
    add_log(log_type::transfer_to, from, quantity, to.to_string());

    // S9 - 수수료 전송 
    string memo="transferto fee";
    if(from != to && fee_amount.amount > 0){
        auto global = global_t.find(0);
        check(global != global_t.end(), "not initialized contract.");
        global_t.modify(global, _self, [&](auto &g){
            g.liabilities_amount -= fee_amount;
        });
        SEND_INLINE_ACTION( *this, transfer, {_self,"active"_n}, {_self, FEE_ACCOUNT, fee_amount, memo} );
    }
}

ACTION wavetoken::subscription(name from, asset quantity, uint64_t product_id, name referral)
{
    require_auth(from);
    auto info = p_info_t.find(product_id);
    check(info != p_info_t.end(), "not exist product_id");
    check(quantity.amount % (info->unit.amount + info->pre_fee.amount) == 0, "invalid quantity");
    //check(quantity.amount - info->unit.amount - info->pre_fee.amount == 0, "invalid quantity");
    
    auto user = users_t.find(from.value);
    check(user != users_t.end(), "invalid user");
    check(user->balance >= quantity, "insufficient balance.");
    
    check(info->limit >= info->invested + quantity - info->pre_fee, "product limit exceeds.");
    
    int multi = quantity.amount / (info->unit.amount + info->pre_fee.amount);
    
    p_info_t.modify(info, _self, [&](auto &i){
        i.sequence += multi;
        i.invested += info->unit * multi;
    });
    
    auto p_user = p_user_t.emplace(_self, [&](auto &p){
        p.id = p_user_t.available_primary_key();//product_id * PRODUCTID + info->sequence;
        p.product_id = product_id;
        p.user = from;
        p.balance = info->unit * multi;
        p.total_interest = asset(p.balance.amount / 10000 * info->interest_rate, quantity.symbol);
        p.remain_interest = p.total_interest;
        p.lockup_interest = asset(0, quantity.symbol);
        p.total_count = info->product_period / info->interest_period;//todo
        p.remain_count = p.total_count;
        p.created_at = eosio::current_time_point().sec_since_epoch();
        p.expired_at = p.created_at + info->product_period;        
        p.next_payment_at = 0;//p.created_at + info->interest_period;
        if(info->interest_rate == 0)
            p.next_payment_at = p.expired_at;
    });
    
    users_t.modify(user, _self, [&](auto &u){
        u.balance -= quantity;
    });

    if(info->pre_fee.amount > 0)
    {
        auto global = global_t.find(0);
        check(global != global_t.end(), "not initialized contract.");
        global_t.modify(global, _self, [&](auto &g){
            g.liabilities_amount -= info->pre_fee;
        });
    }

    check(is_account(referral) == true, "invalid referral");
    check(users_t.find(referral.value) != users_t.end(), "invalid referral(unregistered user)");
    auto ref = referral_t.find(from.value);
    if(ref == referral_t.end())
    {
        referral_t.emplace(_self, [&](auto &r){
            r.user = from;
            r.referral = referral;
        });
    }

    add_log(log_type::subscription, from, quantity, to_string(product_id)+":"+to_string(p_user->id)+":"+referral.to_string());
}
ACTION wavetoken::unsubscript(uint64_t id, uint64_t rate)
{
    require_auth(_self);
    auto p_user = p_user_t.find(id);
    check(p_user != p_user_t.end(), "not exists id");

    auto user = users_t.find(p_user->user.value);
    check(user != users_t.end(), "not exists user");

    auto fee = p_user->balance;
    if(rate > 10000) rate = 10000;
    fee = p_user->balance * rate / 10000;

    users_t.modify(user, _self, [&](auto &u){
        u.balance += p_user->balance - fee;
    });

    auto global = global_t.find(0);
    check(global != global_t.end(), "not initialized contract.");
    global_t.modify(global, _self, [&](auto &g){
        g.liabilities_amount -= fee;
    });

    SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {p_user->user, p_user->balance - fee, "unsubscription"} );    
   
    p_user_t.erase( p_user );
}

ACTION wavetoken::mkproduct(uint64_t product_id, uint64_t rate, uint64_t period, uint64_t interest_period, asset limit, asset unit, asset pre_fee, uint64_t lockup_rate)
{
    require_auth(_self);
    
    auto info = p_info_t.find(product_id);
    check(info == p_info_t.end(), "already exist product_id");
    check(limit.amount % unit.amount == 0, "invalid unit amount");
    check(period % interest_period == 0, "invalid period");
    
    p_info_t.emplace(_self, [&](auto &p){
        p.product_id = product_id;
        p.product_period = period;
        p.interest_rate = rate;
        p.interest_period = interest_period;
        p.limit = limit;
        p.unit = unit;
        p.invested = asset(0, limit.symbol);
        p.pre_fee = pre_fee;
        p.lockup_rate = lockup_rate;
    });
}
ACTION wavetoken::rmproduct(uint64_t product_id)
{
    require_auth(_self);
    
    auto info = p_info_t.find(product_id);
    check(info != p_info_t.end(), "not exist product_id");
    
    check(info->invested.amount == 0, "already started product");
    
    p_info_t.erase( info );
}

ACTION wavetoken::endproduct(uint64_t product_id)
{
    require_auth(_self);
    
    auto info = p_info_t.find(product_id);
    check(info != p_info_t.end(), "not exist product_id");
    
    p_info_t.modify(info, _self, [&](auto &p){
        p.limit = p.invested;
    });
}

ACTION wavetoken::rmproductusr(uint64_t id)
{
    require_auth(_self);
    
    auto p_user = p_user_t.find(id);
    check(p_user != p_user_t.end(), "invalid id");
    check(p_user->remain_count == 0, "all interest is payouted.");
    
    
    p_user_t.erase( p_user );
}


ACTION wavetoken::payment(uint64_t id, uint64_t next_payment_at)
{
    require_auth(_self);
   
    auto p_user = p_user_t.find(id);
    check(p_user != p_user_t.end(), "invalid id");
    check(p_user->remain_count > 0, "all interest is payouted.");
    check(p_user->next_payment_at <= eosio::current_time_point().sec_since_epoch(), "invalid payment time.");
   
    auto product_id = p_user->product_id;
    auto info = p_info_t.find(product_id);
    check(info != p_info_t.end(), "not exist product_id");

    asset interest = asset(p_user->balance.amount * info->interest_rate / 1'0000 / p_user->total_count, p_user->balance.symbol);
    asset lockup_interest = asset(interest.amount * info->lockup_rate / 1'0000, p_user->balance.symbol);
    if(p_user->remain_count == 1)
    {
        interest = p_user->remain_interest;
        lockup_interest = asset(p_user->total_interest.amount * info->lockup_rate / 1'0000, p_user->balance.symbol) - p_user->lockup_interest;
    }

    p_user_t.modify(p_user, _self, [&](auto &p){
        p.remain_interest -= interest;
        p.remain_count --;
        p.next_payment_at = next_payment_at;
        p.lockup_interest += lockup_interest;
    });
   
    auto user_info = users_t.find(p_user->user.value);
    check(user_info != users_t.end(), "not exists user.");
    asset user_balance = user_info->balance + interest - lockup_interest;
    
    auto global = global_t.find(0);
    check(global != global_t.end(), "not initialized contract.");    
    check(global->balance - global->liabilities_amount >= interest, "insufficient balance");
    
    string memo = "id:"+ to_string(id) + ", count:"+ to_string(p_user->total_count - p_user->remain_count);
    //SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {p_user->user, interest - lockup_interest, memo} );    

    SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {p_user->user, interest, memo} );    
    SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {p_user->user, lockup_interest, "locked up interest."} );    

    auto global_interest_amount =    global->interest_amount + interest;
    auto global_lockup_amount =    global->lockup_amount + lockup_interest;
    auto global_liabilities_amount =    global->liabilities_amount + interest;
  

    if(p_user->remain_count == 0)
    {
        asset total_refund = p_user->balance + p_user->lockup_interest;
        user_balance += total_refund;

        global_lockup_amount -= p_user->lockup_interest;

        //SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {p_user->user, total_refund, "refunds"} );

        SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {p_user->user, p_user->balance, "refunds"} );
        SEND_INLINE_ACTION( *this, receipt, {_self,"active"_n}, {p_user->user, p_user->lockup_interest, "unlocked interest."} );    
    } 

    users_t.modify(user_info, _self, [&](auto &u){
        u.balance = user_balance;
    });   

    global_t.modify(global, _self, [&](auto &g){
        g.interest_amount = global_interest_amount;
        g.lockup_amount = global_lockup_amount;
        g.liabilities_amount = global_liabilities_amount;
    });
}
ACTION wavetoken::payment2(uint64_t id, name user, asset quantity)
{
    require_auth(_self);
    check(quantity.is_valid(), "invalid quantity");
    auto global = global_t.find(0);
    check(global != global_t.end(), "not initialized contract.");    
    check(global->balance - global->liabilities_amount >= quantity, "insufficient balance");

    payment_log_t.emplace(_self, [&](auto &l){
        l.id = id;
        l.user = user;
        l.quantity = quantity;
    });

    auto user_info = users_t.find(user.value);
    check(user_info != users_t.end(), "not exists user");
    users_t.modify(user_info, _self, [&](auto &u){
        u.balance += quantity;
    });   

    global_t.modify(global, _self, [&](auto &g){
        g.interest_amount += quantity;
        g.liabilities_amount += quantity;
    });    
}
ACTION wavetoken::rmpayment2(uint64_t id)
{
    require_auth(_self);
    auto log = payment_log_t.find(id);
    check(log != payment_log_t.end(), "not exists log");

    payment_log_t.erase( log );
}
ACTION wavetoken::rmpayment2s(vector<uint64_t> ids)
{
    require_auth(_self);
    for(auto id : ids)
    {
        rmpayment2(id);
    }    
}


// ACTION wavetoken::paymentprod(uint64_t product_id, uint64_t next_payment_at)
// {
//     require_auth(_self);

//     auto info = p_info_t.find(product_id);
//     check(info != p_info_t.end(), "not exist product_id");

//     auto product_idx = p_user_t.get_index<"product.id"_n>();
//     auto users = product_idx.lower_bound(product_id);

//     if(users == product_idx.end()) return;

//     auto global = global_t.find(0);
//     check(global != global_t.end(), "not initialized contract.");       


//     asset g_interest_amount = asset(0, info->unit.symbol);
//     asset g_lockup_amount = asset(0, info->unit.symbol);
//     auto current_time = eosio::current_time_point().sec_since_epoch();
//     while(users != product_idx.end() && users->product_id == product_id)
//     {
//         if(users->next_payment_at > current_time) continue;
//         asset interest = asset(users->balance.amount * info->interest_rate / 1'0000 / users->total_count, users->balance.symbol);
//         if(users->remain_count == 1)
//             interest = users->remain_interest;

//         asset lockup_interest = asset(interest.amount * info->lockup_rate / 1'0000, users->balance.symbol);

//         product_idx.modify(users, _self, [&](auto &p){
//             p.remain_interest -= interest;
//             p.remain_count --;
//             p.next_payment_at = next_payment_at;
//             p.lockup_interest += lockup_interest;
//         });
    
//         auto user_info = users_t.find(users->user.value);
//         check(user_info != users_t.end(), "not exists user.");
//         users_t.modify(user_info, _self, [&](auto &u){
//             u.balance += interest - lockup_interest;
//         });   
        
//         if(users->remain_count > 0)
//         {
//             g_interest_amount += interest;
//             g_lockup_amount += lockup_interest;
//         }
//         else
//         {
//             asset total_refund = users->balance + users->lockup_interest;
//             users_t.modify(user_info, _self, [&](auto &u){
//                 u.balance += total_refund;
//             });   
//             g_lockup_amount -= users->lockup_interest;
//         } 
//         users++;
//     }

//     check(global->balance - global->liabilities_amount >= g_interest_amount, "insufficient balance");    
//     global_t.modify(global, _self, [&](auto &g){
//         g.interest_amount += g_interest_amount;
//         g.lockup_amount += g_lockup_amount;
//         g.liabilities_amount += g_interest_amount;
//     });

// }

ACTION wavetoken::receipt(name user, asset quantity, string memo)
{
    require_auth(_self);
    if(user != _self)
        require_recipient(user);
}

ACTION wavetoken::removelog(uint64_t id)
{
    require_auth(_self);
    
    auto e = log_t.find(id);
    check(e != log_t.end(), "not exists log");
    
    log_t.erase( e );
}
ACTION wavetoken::removelogs(vector<uint64_t> ids)
{
    require_auth(_self);
    
    for(auto id : ids)
    {
        auto e = log_t.find(id);
        check(e != log_t.end(), "not exists log");
        
        log_t.erase( e );
    }
}

ACTION wavetoken::mergeprod(vector<uint64_t> ids)
{
    require_auth(_self);
    
    int64_t merged_id = -1;

    name        user;
    uint64_t    product_id;
    int         total_count;
    int         remain_count;

    asset       balance;
    asset       total_interest;
    asset       remain_interest;
    asset       lockup_interest;

    for(auto id : ids)
    {
        auto p_user = p_user_t.find(id);
        check(p_user != p_user_t.end(), "not exists subscription id");       

        if(user.value == 0){
            merged_id = id;
            user = p_user->user;
            product_id = p_user->product_id;
            balance = p_user->balance;
            total_interest = p_user->total_interest;
            remain_interest = p_user->remain_interest;
            lockup_interest = p_user->lockup_interest;
            total_count = p_user->total_count;
            remain_count = p_user->remain_count;
        }
        else
        {
            check(  user == p_user->user &&
                    product_id == p_user->product_id&&
                    total_count == p_user->total_count&&
                    remain_count == p_user->remain_count
                , "not available to merge");

            balance += p_user->balance;
            total_interest += p_user->total_interest;
            remain_interest += p_user->remain_interest;
            lockup_interest += p_user->lockup_interest;
        }
    }

    if(merged_id>-1){
        for(auto id : ids){
            auto p_user = p_user_t.find(id);
            if(merged_id == id){
                p_user_t.modify(p_user, _self, [&](auto &pu){
                    pu.balance = balance;
                    pu.total_interest = total_interest;
                    pu.remain_interest = remain_interest;
                    pu.lockup_interest = lockup_interest;
                });
            }
            else{
                p_user_t.erase( p_user );
            }
        }
    }
}

ACTION wavetoken::couponbuy(name from, asset quantity, string memo)
{
    require_auth(from);
    auto user_info = users_t.find(from.value);
    check(user_info != users_t.end(), "not exists user.");
    check(user_info->balance >= quantity, "insufficient balance");
    
    users_t.modify(user_info, _self, [&](auto &u){
        u.balance -= quantity;
    });   

    add_log(log_type::buy_coupon, from, quantity, memo);
}

ACTION wavetoken::couponcancel(name to, asset quantity, string memo)
{
    require_auth(_self);

    auto user_info = users_t.find(to.value);
    check(user_info != users_t.end(), "not exists user.");
    
    users_t.modify(user_info, _self, [&](auto &u){
        u.balance += quantity;
    });       
}
ACTION wavetoken::couponreq(name from, asset quantity, string memo)
{
    require_auth(from);

    SEND_INLINE_ACTION( *this, transfer, {from,"active"_n}, {from, _self, quantity, memo} );

    auto counter = counter_t.find(COUPON_COUNTER.value);
    if(counter == counter_t.end())
    {
       counter = counter_t.emplace(_self, [&](auto &c){
          c.key = COUPON_COUNTER;
          c.counter = 0;
       });
    }
    else
    {
        counter_t.modify(counter, _self, [&](auto &c){
            c.counter++;
        });
    }
    coupon_t.emplace(_self, [&](auto &c){
        c.id = counter->counter;
        c.from = from;
        c.quantity = quantity;
        c.memo = memo;
        c.created_at = eosio::current_time_point().sec_since_epoch();
    });
}
ACTION wavetoken::couponapprov(uint64_t id, bool is_approved)
{
    require_auth(_self);

    auto coupon = coupon_t.find(id);
    check(coupon != coupon_t.end(), "not exists coupon id");

    if(is_approved == false)
        SEND_INLINE_ACTION( *this, transfer, {_self,"active"_n}, {_self, coupon->from , coupon->quantity, coupon->memo} );

    coupon_t.erase( coupon );     
}

// ACTION wavetoken::removeuser(){
//     do{
//       auto user = users_t.begin();
//       if(user == users_t.end()) break;
//       users_t.erase( user );
//     }while(true);
//     do{
//       auto global = global_t.begin();
//       if(global == global_t.end()) break;
//       global_t.erase( global );
//     }while(true);
//     do{
//       auto p_user = p_user_t.begin();
//       if(p_user == p_user_t.end()) break;
//       p_user_t.erase( p_user );
//     }while(true);    
//     do{
//       auto product = p_info_t.begin();
//       if(product == p_info_t.end()) break;
//       p_info_t.erase( product );
//     }while(true);    
// }

// ACTION wavetoken::removesym(symbol sym)
// {
//     require_auth(_self);
    
//     stats statstable_t(_self, sym.code().raw());
//     auto existing = statstable_t.find( sym.code().raw());
//     check(existing != statstable_t.end(), "token symbol is not exists.");
    
//     statstable_t.erase(existing);  
// }
