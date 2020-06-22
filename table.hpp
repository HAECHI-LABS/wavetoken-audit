#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <string>

using namespace eosio;
using namespace std;

#define ABITABLE struct [[using eosio: table, contract("wavetoken")]]
#define ABITABLE_NAMED(name) struct [[using eosio: table(name), contract("wavetoken")]]

ABITABLE global{
    uint64_t  id;
    uint64_t  withdrawal_fee;
    uint64_t  referral_fee;
    asset     balance;
    asset     referral_amount;
    asset     interest_amount;
    asset     withdraw_amount;
    asset     lockup_amount;
    asset     liabilities_amount;
    uint64_t primary_key()const { return id; }
};
typedef eosio::multi_index<"global"_n, global> global_table;


#define LOG_COUNTER ("log.counter"_n)
#define COUPON_COUNTER ("coupon.cnt"_n)

ABITABLE counter{
    name      key;
    uint64_t  counter;
    uint64_t primary_key()const { return key.value; }
};
typedef eosio::multi_index<"counter"_n, counter> counter_table;

ABITABLE account {
    asset    balance;

    uint64_t primary_key()const { return balance.symbol.code().raw(); }
};
typedef eosio::multi_index<"accounts"_n, account> accounts;


ABITABLE currency_stat {
    asset       supply;
    asset       max_supply;
    name        issuer;

    uint64_t primary_key()const { return supply.symbol.code().raw(); }
};
typedef eosio::multi_index<"stat"_n, currency_stat> stats;


// 유저 
ABITABLE users{
    name              user;
    asset             balance;
    name              referral;
    uint64_t primary_key()const { return user.value; }
};
typedef eosio::multi_index<"users"_n, users> users_table;


ABITABLE tx_log{
    uint64_t          id;
    int               type;
    name              user;
    asset             quantity;
    string            memo;
    checksum256       tx_id;
    uint64_t          created_at;
    uint64_t primary_key()const { return id; }
};
typedef eosio::multi_index<"tx.log"_n, tx_log> log_table;

//type
typedef enum log_type{
    deposit = 1,
    withdraw = 2,
    transfer_to = 3,
    subscription = 4,
    buy_coupon = 5,
    payment2 = 6,
    referral_to = 7,
}log_type;

// 상품
ABITABLE product_info{
    uint64_t    product_id;
    uint64_t    product_period;
    uint64_t    interest_rate;
    uint64_t    interest_period;
    uint64_t    lockup_rate;
    asset       limit;
    asset       unit;
    asset       invested;
    asset       pre_fee;
    int         sequence;
    
    uint64_t primary_key()const { return product_id; }
};
typedef eosio::multi_index<"product.info"_n, product_info> product_info_table;


//상품 가입자
ABITABLE product_users{
    uint64_t    id;
    uint64_t    product_id;
    name        user;
    asset       balance;
    asset       total_interest;
    asset       remain_interest;
    asset       lockup_interest;
    int         total_count;
    int         remain_count;
    uint64_t    created_at;
    uint64_t    expired_at;
    uint64_t    next_payment_at;
    uint64_t    primary_key()const { return id; }
    uint64_t    by_product_id()const { return product_id; }
    uint64_t    by_user()const {return user.value;}
    uint64_t    by_created()const{return created_at;}
    uint64_t    by_next()const{return next_payment_at;}
};
typedef eosio::multi_index<"product.user"_n, product_users
            ,indexed_by<"product.id"_n, const_mem_fun<product_users, uint64_t, &product_users::by_product_id>>
            ,indexed_by<"user"_n, const_mem_fun<product_users, uint64_t, &product_users::by_user>>
            ,indexed_by<"created"_n, const_mem_fun<product_users, uint64_t, &product_users::by_created>>
            ,indexed_by<"next"_n, const_mem_fun<product_users, uint64_t, &product_users::by_next>>
            > product_users_table;


ABITABLE payment_log{
    uint64_t          id;
    name              user;
    asset             quantity;
    uint64_t primary_key()const { return id; }
};
typedef eosio::multi_index<"payment.log"_n, payment_log> payment_log_table;


ABITABLE referral{
    name              user;
    name              referral;
    uint64_t primary_key()const { return user.value; }
};
typedef eosio::multi_index<"referral"_n, referral> referral_table;

//구폰 구입 요청
ABITABLE coupon_req{
    uint64_t    id;
    name        from;
    asset       quantity;
    string      memo;
    uint64_t    created_at;
    uint64_t    primary_key()const { return id; }
};
typedef eosio::multi_index<"coupon.req"_n, coupon_req> coupon_req_table;

// S9 lock 테이블 추가
ABITABLE accountslock{
    asset        balance;

    uint64_t primary_key() const {return balance.symbol.code().raw();}
};
typedef eosio::multi_index<"accountslock"_n, accountslock> accounts_locked;
