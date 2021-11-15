/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef>
#include <memory>
#include <list>
#include <string>
#include <string_view>

#include <zlib.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h> /* TODO: eventually remove this */

#include "transmission.h"

#include "net.h"
#include "rpcimpl.h"

struct tr_variant;

class tr_rpc_server
{
public:
    struct Dependencies
    {
        virtual ~Dependencies() = default;
        virtual event_base* eventBase() const = 0;
        virtual std::string sessionId() const = 0;
        virtual std::string webClientDir() const = 0;
        virtual void execJson(tr_variant const* parsed, tr_rpc_response_func, void* callback_user_data) = 0;
        virtual void execUri(std::string_view uri, tr_rpc_response_func, void* callback_user_data) = 0;
        virtual void lock() = 0; // TODO(ckerr): RAII
        virtual void unlock() = 0;
    };

    tr_rpc_server(std::unique_ptr<Dependencies>&& deps, tr_variant* settings);
    ~tr_rpc_server();

    // block brute-force attacks

    size_t constexpr maxLoginAttempts() const
    {
        return max_login_attempts_;
    }

    size_t constexpr loginAttempts() const
    {
        return login_attempts_;
    }

    void constexpr setMaxLoginAttempts(size_t n)
    {
        max_login_attempts_ = n;
    }

    bool constexpr useMaxLoginAttempts() const
    {
        return max_login_attempts_enabled_;
    }

    void constexpr useMaxLoginAttempts(bool enabled)
    {
        max_login_attempts_enabled_ = enabled;
        login_attempts_ = 0;
    }

    bool constexpr maxLoginAttemptsReached() const
    {
        return useMaxLoginAttempts() && login_attempts_ >= maxLoginAttempts();
    }

    void constexpr loginFailed()
    {
        ++login_attempts_;
    }

    void constexpr loginSucceeded()
    {
        login_attempts_ = 0;
    }

public:
    z_stream stream = {};

    std::list<std::string> hostWhitelist;
    std::list<std::string> whitelist;
    std::string salted_password;
    std::string username;
    std::string whitelistStr;
    std::string url;

    struct tr_address bindAddress;

    struct event* start_retry_timer = nullptr;
    struct evhttp* httpd = nullptr;

    size_t max_login_attempts_;
    size_t login_attempts_;

    int antiBruteForceThreshold = 0;
    int start_retry_counter = 0;

    tr_port port = 0;

    bool max_login_attempts_enabled_ = false;
    bool isEnabled = false;
    bool isHostWhitelistEnabled = false;
    bool isPasswordEnabled = false;
    bool isStreamInitialized = false;
    bool isWhitelistEnabled = false;

    std::unique_ptr<Dependencies> deps_;
};

void tr_rpcSetEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcIsEnabled(tr_rpc_server const* server);

void tr_rpcSetPort(tr_rpc_server* server, tr_port port);

tr_port tr_rpcGetPort(tr_rpc_server const* server);

void tr_rpcSetUrl(tr_rpc_server* server, std::string_view url);

std::string const& tr_rpcGetUrl(tr_rpc_server const* server);

int tr_rpcSetTest(tr_rpc_server const* server, char const* whitelist, char** allocme_errmsg);

void tr_rpcSetWhitelistEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcGetWhitelistEnabled(tr_rpc_server const* server);

void tr_rpcSetWhitelist(tr_rpc_server* server, std::string_view whitelist);

std::string const& tr_rpcGetWhitelist(tr_rpc_server const* server);

void tr_rpcSetPassword(tr_rpc_server* server, std::string_view password);

std::string const& tr_rpcGetPassword(tr_rpc_server const* server);

void tr_rpcSetUsername(tr_rpc_server* server, std::string_view username);

std::string const& tr_rpcGetUsername(tr_rpc_server const* server);

void tr_rpcSetPasswordEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcIsPasswordEnabled(tr_rpc_server const* session);

char const* tr_rpcGetBindAddress(tr_rpc_server const* server);
