#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <string>
#include "table.hpp"

using namespace eosio;
using namespace std;

using std::string;


// 메인넷 / 테스트넷 계정을 설정한다.
// 수수료 처리 계정을 설정하기 위해
//#define KYLINNET

#ifdef KYLINNET
    // kylin net
    // 컨트랙트 계정
    #define CONTRACT_ACCOUNT    name("wavetokenpb1")    
    // 수수료를 받을 계정
    #define FEE_ACCOUNT         name("wavetokenpb1")
    // 거래소 계정
    #define EXCHANGE_ACCOUNT    name("wavetokenpb1")
#else
    // mainnet
    // 컨트랙트 계정
    #define CONTRACT_ACCOUNT    name("waveglobalio")
    // 수수료를 받을 계정
    #define FEE_ACCOUNT         name("waveglobalio")
    // 거래소 계정
    #define EXCHANGE_ACCOUNT    name("namebitdepo1")
#endif

// 전송 수수료 0.1%
#define TRANSFER_FEE_RATE       0.001
// 전송 총금액에서 수수료 구하기 전송수수료 = 총금액(전송액+수수료) / 1001
#define TRANSFER_FEE_DIVISOR    1001
// AllA 내 전송 수수료 10%
#define TRANSFER_TO_FEE_RATE    0.1
// 전송 총금액에서 수수료 구하기 전송수수료 = 총금액(전송액+수수료) / 11
#define TRANSFER_TO_FEE_DIVISOR    11


CONTRACT wavetoken : public contract {
    private:

        inline asset get_supply( symbol sym )const;
        inline asset get_balance( name owner, symbol sym )const;

        global_table        global_t;        
        users_table         users_t;
        product_info_table  p_info_t;
        product_users_table p_user_t;
        log_table           log_t;
        counter_table       counter_t;
        payment_log_table   payment_log_t;
        referral_table      referral_t;
        coupon_req_table    coupon_t;
    public:

        wavetoken(name receiver, name code, datastream<const char*> ds) 
        : contract(receiver, code, ds) 
        , global_t(receiver, receiver.value)
        , users_t(receiver, receiver.value)
        , p_info_t(receiver, receiver.value)
        , p_user_t(receiver, receiver.value)
        , log_t(receiver, receiver.value)
        , counter_t(receiver, receiver.value)
        , payment_log_t(receiver, receiver.value)
        , referral_t(receiver, receiver.value)
        , coupon_t(receiver, receiver.value)
        {}


        ///////////////////////////////////////////////////////////////////
        //토큰 생성 관련
        ACTION create( name issuer, asset  maximum_supply);
        ACTION issue( name to, asset quantity, string memo );
        ACTION transfer( name from, name to, asset quantity, string memo );
        ACTION lock(name user, asset quantity);
        ACTION unlock(name user, asset quantity);
        //quantity만큼의 물량을 비발행 물량으로 돌려서 유통량이 줄어든다.
        ACTION retire(name from, asset quantity, string memo);

        void sub_balance( name owner, asset value, asset total_value, const currency_stat& st );
        void add_balance( name owner, asset value, const currency_stat& st, name ram_payer );
        
        
        
        inline checksum256 get_seed_from_tx();

        ///////////////////////////////////////////////////////////////////
        ///product
        ACTION initcontract(uint64_t withdrawal_fee, uint64_t referral_fee, symbol sym);
        ACTION initglobalva(int mode);
        
        ACTION mkproduct(uint64_t product_id, uint64_t rate, uint64_t period, uint64_t interest_period, asset limit, asset unit, asset pre_fee, uint64_t lockup_rate);
        ACTION rmproduct(uint64_t product_id);
        ACTION endproduct(uint64_t product_id);
        ACTION rmproductusr(uint64_t id);
        ACTION payment(uint64_t id, uint64_t next_payment_at);
        //ACTION paymentprod(uint64_t product_id, uint64_t next_payment_at);
        ACTION receipt(name user, asset quantity, string memo);

        ACTION payment2(uint64_t id, name user, asset quantity);
        ACTION rmpayment2(uint64_t id);
        ACTION rmpayment2s(vector<uint64_t> ids);

        ACTION deposit(name from, asset quantity, name referral);
        ACTION transferto(name from, name to, asset quantity);
        ACTION subscription(name from, asset quantity, uint64_t product_id, name referral);
        ACTION withdraw(name from, asset quantity);
        ACTION unsubscript(uint64_t id, uint64_t rate);
        ACTION referralto(name to, asset quantity);


        ACTION couponbuy(name from, asset quantity, string memo);
        ACTION couponcancel(name from, asset quantity, string memo);


        //쿠폰 계정에서 직접구입
        ACTION couponreq(name from, asset quantity, string memo);
        ACTION couponapprov(uint64_t id, bool is_approved);

        ACTION adduser(name user);


        ACTION mergeprod(vector<uint64_t> ids);

        
        ACTION removelog(uint64_t id);
        ACTION removelogs(vector<uint64_t> ids);
        
        //ACTION removeuser();
        //ACTION removesym(symbol sym);
        
        
        void add_log(int type, name user, asset quantity, string memo);
};

asset wavetoken::get_supply( symbol sym )const
{
    stats statstable( _self, sym.code().raw() );
    const auto& st = statstable.get( sym.code().raw() );
    return st.supply;
}

asset wavetoken::get_balance( name owner, symbol sym )const
{
    accounts accountstable( _self, owner.value );
    const auto& ac = accountstable.get( sym.code().raw() );
    return ac.balance;
}
