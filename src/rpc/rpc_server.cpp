#include "rpc_server.hpp"

#include "config/config.hpp"
#include "constants/version.hpp"
#include "contact/client_contact.hpp"
#include "contact/contactdb.hpp"
#include "link/link_manager.hpp"
#include "router/router.hpp"
#include "rpc/rpc_request_definitions.hpp"
#include "rpc_request.hpp"

#include <nlohmann/json.hpp>
#include <oxenc/base32z.h>

#ifndef _WIN32
#include <grp.h>
#include <sys/stat.h>

#include <array>
#include <cerrno>
#include <cstring>
#endif

#include <exception>
#include <stdexcept>
#include <vector>

namespace srouter::rpc
{
    static auto logcat = srouter::log::Cat("rpc-server");

#ifndef _WIN32
    // Tighten the permissions on a just-bound ipc:// control socket so it is not world-connectable.
    // OMQ creates the socket with umask-derived permissions, so without this an unauthenticated
    // local user could reach the (admin-level) control RPC. 0600 by owner only; 0660 plus a group
    // chown when the operator names an admin group via [api] rpc-group.
    static void secure_ipc_socket(const std::string& bind_addr, const std::string& group)
    {
        // Strip the "ipc://" scheme to get the filesystem path.
        std::string path = bind_addr.substr(bind_addr.find("://") + 3);
        mode_t mode = 0600;

        if (not group.empty())
        {
            std::array<char, 4096> buf{};
            struct group grp{};
            struct group* result = nullptr;
            if (getgrnam_r(group.c_str(), &grp, buf.data(), buf.size(), &result) == 0 and result)
            {
                if (chown(path.c_str(), static_cast<uid_t>(-1), grp.gr_gid) == 0)
                    mode = 0660;
                else
                    log::warning(
                        logcat, "Could not chown RPC socket {} to group {}: {}", path, group, std::strerror(errno));
            }
            else
                log::warning(logcat, "[api] rpc-group '{}' not found; leaving RPC socket owner-only", group);
        }

        if (chmod(path.c_str(), mode) != 0)
            log::warning(logcat, "Could not set permissions on RPC socket {}: {}", path, std::strerror(errno));
        else
            log::debug(logcat, "Secured RPC socket {} to mode {:o}", path, mode);
    }
#endif

    template <typename T>
        requires std::derived_from<T, RPCRequest>
    static void log_print_rpc(T& req)
    {
        log::info(logcat, "RPC Server received request for endpoint `{}`", req.name);
    }

#if 0
    // Fake packet source that serializes repsonses back into dns
    class DummyPacketSource final : public dns::PacketSource
    {
        std::function<void(std::optional<dns::Message>)> func;

      public:
        explicit DummyPacketSource(std::function<void(std::optional<dns::Message>)> func) : func{std::move(func)} {}

        bool would_loop(const quic::Address&, const quic::Address&) const override { return false; };

        /// send packet with src and dst address containing buf on this packet source
        void send_udp(const quic::Address&, const quic::Address&, std::span<const std::byte> payload) const override
        {
            func(dns::Message::extract(payload));
        }

        /// returns the sockaddr we are bound on if applicable
        std::optional<quic::Address> bound_on() const override { return std::nullopt; }
    };
#endif

    bool check_path(std::string path)
    {
        for (auto c : path)
        {
            if (not((c >= '0' and c <= '9') or (c >= 'A' and c <= 'Z') or (c >= 'a' and c <= 'z') or (c == '_')
                    or (c == '-')))
            {
                return false;
            }
        }

        return true;
    }

    template <typename RPC>
    void register_rpc_command(std::unordered_map<std::string, rpc_callback>& regs)
    {
        static_assert(std::is_base_of_v<RPCRequest, RPC>);
        rpc_callback cback{};

        cback.invoke = make_invoke<RPC>();
        cback.auth_level = RPC::auth_level;

        regs.emplace(RPC::name, std::move(cback));
    }

    RPCServer::RPCServer(oxenmq::OxenMQ& omq, Router& r) : _omq{omq}, _router(r)
    {
        if (srouter::logRingBuffer)
            log_subs.emplace(_omq, srouter::logRingBuffer);

        const auto& api = _router.config().api;

        // Bound the control-RPC message size on this dedicated OMQ instance. The oxend client runs
        // on a separate instance that keeps the unlimited size needed for the service-node list, so
        // capping here does not affect it.
        if (api.rpc_max_request_size > 0)
            _omq.MAX_MSG_SIZE = api.rpc_max_request_size;

        for (const auto& addr : api.rpc_bind_addrs)
        {
            const bool is_ipc = addr.starts_with("ipc://");
            const bool is_loopback = addr.find("127.0.0.1") != std::string::npos
                || addr.find("[::1]") != std::string::npos || addr.find("localhost") != std::string::npos;

            if (not is_ipc and not is_loopback)
            {
                // A non-loopback TCP bind exposes router control to the network. Refuse by default;
                // the operator must explicitly accept the exposure.
                if (not api.allow_insecure_rpc)
                    throw std::runtime_error{
                        "Refusing to bind the control RPC to non-loopback address " + addr
                        + ": prefer an ipc:// socket or tcp://127.0.0.1, or set [api] allow-insecure-rpc=true "
                          "only if you understand the exposure."};
                log::warning(
                    logcat,
                    "RPC bound to non-loopback address {} with allow-insecure-rpc set: the control RPC is "
                    "reachable from the network.",
                    addr);
            }

            // Grant admin on the local IPC socket (gated by the filesystem permissions set below in
            // the on-bind callback). A plain TCP connection is basic (read-only) unless the operator
            // opted into insecure admin-over-TCP.
            const oxenmq::AuthLevel level =
                (is_ipc or api.allow_insecure_rpc) ? oxenmq::AuthLevel::admin : oxenmq::AuthLevel::basic;

            const std::string bind_addr = addr;
#ifndef _WIN32
            const std::string rpc_group = api.rpc_group;
#endif
            _omq.listen_plain(
                addr,
                [level](std::string_view, std::string_view, bool) { return level; },
                [this, bind_addr, is_ipc
#ifndef _WIN32
                 ,
                 rpc_group
#endif
            ](bool success) {
                    if (not success)
                    {
                        log::error(logcat, "Failed to bind RPC server to {}", bind_addr);
                        return;
                    }
#ifndef _WIN32
                    if (is_ipc)
                        secure_ipc_socket(bind_addr, rpc_group);
#endif
                    log::debug(logcat, "Bound RPC server to {}", bind_addr);
                });
        }

        AddCategories();
    }

    template <typename... RPC>
    std::unordered_map<std::string, rpc_callback> register_rpc_requests(tools::type_list<RPC...>)
    {
        std::unordered_map<std::string, rpc_callback> regs;

        (register_rpc_command<RPC>(regs), ...);

        return regs;
    }

    const std::unordered_map<std::string, rpc_callback> rpc_request_map =
        register_rpc_requests(rpc::rpc_request_types{});

    void RPCServer::AddCategories()
    {
        // Two tiers: the "srouter" category holds read-only commands at basic auth; the "admin"
        // category holds mutating/sensitive commands at admin auth. A connection's auth level is
        // set per-listener (admin on the local IPC socket, basic on plain TCP), so a mutating call
        // over an unprivileged connection is rejected by OMQ before it reaches a handler.
        _omq.add_category("srouter", oxenmq::AuthLevel::basic);
        _omq.add_category("admin", oxenmq::AuthLevel::admin)
            .add_request_command("logs", [this](oxenmq::Message& msg) { HandleLogsSubRequest(msg); });

        for (auto& req : rpc_request_map)
        {
            const char* category = req.second.auth_level == oxenmq::AuthLevel::basic ? "srouter" : "admin";
            _omq.add_request_command(
                category, req.first, [name = std::string_view{req.first}, &call = req.second, this](oxenmq::Message& m) {
                    call.invoke(m, *this);
                });
        }
    }

    void RPCServer::invoke(Halt& halt)
    {
        log_print_rpc(halt);

        if (not _router.is_running())
        {
            SetJSONError("Router is not running", halt.response);
            return;
        }

        _router._jq->call_soon([&]() { _router.stop(); });

        SetJSONResponse("OK", halt.response);
    }

    void RPCServer::invoke(Version& version)
    {
        log_print_rpc(version);

        nlohmann::json result{
            {"version", srouter::VERSION}, {"version_full", srouter::VERSION_FULL}, {"uptime", to_json(uptime())}};

        SetJSONResponse(result, version.response);
    }

    // Builds a snapshot of the router's live resource counters.  These live on the router loop
    // (path_ctx_stats() even asserts it), so this hops onto the loop to read a consistent snapshot;
    // call_get blocks the calling RPC worker thread until the loop returns, and runs inline if we are
    // already on the loop.  Shared by the `status` and `get_status` commands.
    static nlohmann::json collect_status(Router& router)
    {
        return router._jq->call_get([&router]() -> nlohmann::json {
            auto [n_paths, n_transit] = router.path_context.path_ctx_stats();
            auto& endpoint = router.link_manager().endpoint;
            return {
                {"num_client_contacts", router.contact_db().num_ccs()},
                {"num_paths", n_paths},
                {"num_transit_paths", n_transit},
                {"num_inbound_clients", endpoint.num_inbound_clients()},
                {"num_relay_conns", endpoint.num_relay_conns()},
            };
        });
    }

    void RPCServer::invoke(Status& status)
    {
        log_print_rpc(status);
        SetJSONResponse(collect_status(_router), status.response);
    }

    void RPCServer::invoke(GetStatus& getstatus)
    {
        log_print_rpc(getstatus);
        SetJSONResponse(collect_status(_router), getstatus.response);
    }

    void RPCServer::invoke(QuicConnect& quicconnect)
    {
        log_print_rpc(quicconnect);

        auto& req = quicconnect.request;

        if (req.port == 0 and req.closeID == 0)
        {
            SetJSONError("Port not provided", quicconnect.response);
            return;
        }

        if (req.remoteHost.empty() and req.closeID == 0)
        {
            SetJSONError("Host not provided", quicconnect.response);
            return;
        }

        // auto endpoint =
        //     (req.endpoint.empty()) ? GetEndpointByName(_router, "default") : GetEndpointByName(_router,
        //     req.endpoint);

        // if (not endpoint)
        // {
        //     SetJSONError("No such local endpoint found.", quicconnect.response);
        //     return;
        // }

        // auto quic = endpoint->GetQUICTunnel();

        // if (not quic)
        // {
        //     SetJSONError("No quic interface available on endpoint " + req.endpoint, quicconnect.response);
        //     return;
        // }

        if (req.closeID)
        {
            // TODO:
            // quic->forget(req.closeID);
            SetJSONResponse("OK", quicconnect.response);
            return;
        }

        quic::Address laddr{req.bindAddr, req.port};

        try
        {
            // TODO:
            // auto [addr, id] = quic->open(
            //     req.remoteHost, req.port, [](auto&&) {}, laddr);

            nlohmann::json status;
            // status["addr"] = addr.to_string();
            // status["id"] = id;

            SetJSONResponse(status, quicconnect.response);
        }
        catch (std::exception& e)
        {
            SetJSONError(e.what(), quicconnect.response);
        }
    }

    void RPCServer::invoke(QuicListener& quiclistener)
    {
        log_print_rpc(quiclistener);

        auto req = quiclistener.request;

        if (req.port == 0 and req.closeID == 0)
        {
            SetJSONError("Invalid arguments", quiclistener.response);
            return;
        }

        // auto endpoint =
        //     (req.endpoint.empty()) ? GetEndpointByName(_router, "default") : GetEndpointByName(_router,
        //     req.endpoint);

        // if (not endpoint)
        // {
        //     SetJSONError("No such local endpoint found", quiclistener.response);
        //     return;
        // }

        // auto quic = endpoint->GetQUICTunnel();

        // if (not quic)
        // {
        //     SetJSONError("No quic interface available on endpoint " + req.endpoint, quiclistener.response);
        //     return;
        // }

        if (req.closeID)
        {
            // TODO:
            // quic->forget(req.closeID);
            SetJSONResponse("OK", quiclistener.response);
            return;
        }

        if (req.port)
        {
            auto id = 0;
            try
            {
                quic::Address addr{req.remoteHost, req.port};
                // TODO:
                // id = quic->listen(addr);
            }
            catch (std::exception& e)
            {
                SetJSONError(e.what(), quiclistener.response);
                return;
            }

            nlohmann::json result;
            result["id"] = id;
            std::string localAddress;
            // var::visit([&](auto&& addr) { localAddress = addr.to_string(); }, endpoint->local_address());
            result["addr"] = localAddress + ":" + std::to_string(req.port);

            if (not req.srvProto.empty())
            {
                dns::SRVData srvData{req.srvProto, 1, 1, req.port, ""};
                // endpoint->put_srv_record(std::move(srvData));
            }

            SetJSONResponse(result, quiclistener.response);
            return;
        }
    }

    static std::optional<NetworkAddress> try_netaddr(std::string_view x)
    {
        try
        {
            return NetworkAddress{x};
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void RPCServer::invoke(FindCC& findcc)
    {
        log_print_rpc(findcc);

        if (_router.is_service_node)
        {
            SetJSONError("Not supported", findcc.response);
            return;
        }

        if (findcc.request.pk.empty())
        {
            SetJSONError("No pubkey provided!", findcc.response);
            return;
        }

        auto maybe_netaddr = try_netaddr(findcc.request.pk);

        if (not maybe_netaddr)
        {
            SetJSONError("Invalid pubkey provided: {}"_format(findcc.request.pk), findcc.response);
            return;
        }

        _router._jq->call([this, netaddr = *maybe_netaddr, replier = findcc.move()]() mutable {
            // Move the replier into the inner callback: lookup_client_intro stores it for an async
            // network response that fires after this outer job has returned and been destroyed, so
            // capturing it by reference was a use-after-free.
            _router.session_endpoint().lookup_client_intro(
                netaddr.pubkey, [replier = std::move(replier)](std::optional<srouter::ClientContact> cc) mutable {
                    nlohmann::json result;
                    if (cc)
                    {
                        auto cc_str = "{}"_format(*cc);
                        result.emplace("cc", cc_str);
                        log::info(logcat, "RPC call to `find_cc` returned successfully: {}", cc_str);
                    }
                    else
                    {
                        log::warning(logcat, "RPC call to `find_cc` failed!");
                        result.emplace("cc", "ERROR");
                    }
                    replier.reply(result.dump());
                });
        });
    }

    void RPCServer::invoke(SessionInit& sessioninit)
    {
        log_print_rpc(sessioninit);

        if (_router.is_service_node)
        {
            SetJSONError("Not supported", sessioninit.response);
            return;
        }

        if (sessioninit.request.pk.empty())
        {
            SetJSONError("No pubkey provided!", sessioninit.response);
            return;
        }

        auto maybe_netaddr = try_netaddr(sessioninit.request.pk);

        if (not maybe_netaddr)
        {
            SetJSONError("Invalid pubkey provided: {}"_format(sessioninit.request.pk), sessioninit.response);
            return;
        }

        _router._jq->call([this, netaddr = *maybe_netaddr]() {
            try
            {
                log::debug(logcat, "Beginning session init to remote instance: {}", netaddr);
                _router.session_endpoint().initiate_remote_session(netaddr, nullptr);
                /*[replier = sessioninit.move()](auto success) mutable {
                    // FIXME: needs redone, initiate remote session no longer returns an ip
                    nlohmann::json result;
                    std::string a = std::holds_alternative<ipv4>(ip) ? std::get<ipv4>(ip).to_string()
                                                                     : std::get<ipv6>(ip).to_string();
                    result.emplace("ip", a);
                    log::info(logcat, "RPC call to `session_init` succeeded: {}", a);
                    replier.reply(result.dump());
                });
                */
                log::info(logcat, "RPC Server dispatched `session_init` to remote:{}", netaddr);
            }
            catch (const std::exception& e)
            {
                log::critical(logcat, "Failed to parse remote instance netaddr: {}", e.what());
            }
        });
    }

    void RPCServer::invoke(SessionClose& sessionclose)
    {
        log_print_rpc(sessionclose);

        if (sessionclose.request.pk.empty())
        {
            SetJSONError("No pubkey provided!", sessionclose.response);
            return;
        }

        auto maybe_netaddr = try_netaddr(sessionclose.request.pk);

        if (not maybe_netaddr)
        {
            SetJSONError("Invalid pubkey provided: {}"_format(sessionclose.request.pk), sessionclose.response);
            return;
        }

        auto& netaddr = *maybe_netaddr;

        _router._jq->call([&]() {
            try
            {
                if (auto session = _router.session_endpoint().get_session(netaddr))
                {
                    auto hook = [replier = sessionclose.move()](quic::message m) mutable {
                        nlohmann::json result;

                        if (m)
                        {
                            result.emplace("result", "OK");
                            log::info(logcat, "RPC call to `session_close` succeeded!");
                            return replier.reply(result.dump());
                        }

                        std::string status{"<none given>"};

                        try
                        {
                            oxenc::bt_dict_consumer btdc{m.body()};

                            if (auto s = btdc.maybe<std::string>("!"sv))
                                status = std::move(*s);
                        }
                        catch (const std::exception& e)
                        {
                            status = "Exception: {}"_format(e.what());
                        }

                        log::critical(logcat, "Call to `session_close` FAILED; reason: {}", status);
                        result.emplace("result", std::move(status));
                        replier.reply(result.dump());
                    };

                    // session->stop_session(true, std::move(hook));

                    log::info(logcat, "RPC Server dispatched `session_close` to remote:{}", netaddr);
                }
            }
            catch (const std::exception& e)
            {
                log::critical(logcat, "Failed to parse remote instance netaddr: {}", e.what());
            }
        });
    }

    // TODO: fix this because it's bad
    void RPCServer::invoke(LookupSnode& lookupsnode)
    {
        log_print_rpc(lookupsnode);

        if (not _router.is_service_node)
        {
            SetJSONError("Not supported", lookupsnode.response);
            return;
        }

        RouterID routerID;

        if (lookupsnode.request.routerID.empty())
        {
            SetJSONError("No remote ID provided", lookupsnode.response);
            return;
        }

        if (not routerID.from_relay_address(lookupsnode.request.routerID))
        {
            SetJSONError("Invalid remote: " + lookupsnode.request.routerID, lookupsnode.response);
            return;
        }

        // _router.loop()->call([&]() {
        //   auto endpoint = _router.exit_context().get_exit_endpoint("default");

        //   if (endpoint == nullptr)
        //   {
        //     SetJSONError("Cannot find local endpoint: default", lookupsnode.response);
        //     return;
        //   }

        //   endpoint->ObtainSNodeSession(routerID, [&](auto session) {
        //     if (session and session->IsReady())
        //     {
        //       const auto ip = net::TruncateV6(endpoint->GetIPForIdent(PubKey{routerID}));
        //       nlohmann::json status{{"ip", ip.to_string()}};
        //       SetJSONResponse(status, lookupsnode.response);
        //       return;
        //     }

        //     SetJSONError("Failed to obtain snode session", lookupsnode.response);
        //     return;
        //   });
        // });
    }

    void RPCServer::invoke(MapExit& mapexit)
    {
        log_print_rpc(mapexit);

        MapExit exit_request;
        // steal replier from exit RPC endpoint
        exit_request.replier.emplace(mapexit.move());

        // TODO: connect this to remote service session management (service::Handler)
        // _router.hidden_service_context().GetDefault()->map_exit(
        //     mapexit.request.address,
        //     mapexit.request.token,
        //     mapexit.request.ip_range,
        //     [exit = std::move(exit_request)](bool success, std::string result) mutable {
        //       if (success)
        //         exit.send_response({{"result"}, std::move(result)});
        //       else
        //         exit.send_response({{"error"}, std::move(result)});
        //     });
    }

    void RPCServer::invoke(ListExits& listexits)
    {
        log_print_rpc(listexits);

        // TODO: this
    }

    void RPCServer::invoke(UnmapExit& unmapexit)
    {
        log_print_rpc(unmapexit);

        try
        {
            // for (auto& ip : unmapexit.request.ip_range)
            //   _router.hidden_service_context().GetDefault()->UnmapExitRange(ip);
        }
        catch (std::exception& e)
        {
            SetJSONError("Unable to unmap to given range", unmapexit.response);
            return;
        }

        SetJSONResponse("OK", unmapexit.response);
    }

    //  Sequentially calls map_exit and unmap_exit to hotswap mapped connection from old exit
    //  to new exit. Similar to how map_exit steals the oxenmq deferredsend object, swapexit
    //  moves the replier object to the unmap_exit struct, as that is called second. Rather than
    //  the nested lambda within map_exit making the reply call, it instead calls the unmap_exit
    //  logic and leaves the message handling to the unmap_exit struct
    void RPCServer::invoke(SwapExits& swapexits)
    {
        log_print_rpc(swapexits);

        // TODO: this
    }

#if 0
    void RPCServer::invoke(DNSQuery& dnsquery)
    {
        log_print_rpc(dnsquery);

        std::string qname = (dnsquery.request.qname.empty()) ? "" : dnsquery.request.qname;
        dns::QType_t qtype = (dnsquery.request.qtype) ? dnsquery.request.qtype : dns::qTypeA;

        dns::Message msg{dns::Question{qname, qtype}};

        // auto endpoint = (dnsquery.request.endpoint.empty()) ? GetEndpointByName(_router, "default")
        //                                                     : GetEndpointByName(_router, dnsquery.request.endpoint);

        // if (endpoint == nullptr)
        // {
        //     SetJSONError("No such endpoint found for dns query", dnsquery.response);
        //     return;
        // }

        // if (auto dns = endpoint->DNS())
        // {
        //     auto packet_src = std::make_shared<DummyPacketSource>([&](auto result) {
        //         if (result)
        //             SetJSONResponse(result->ToJSON(), dnsquery.response);
        //         else
        //             SetJSONError("No response from DNS", dnsquery.response);
        //     });
        //     if (not dns->maybe_handle_packet(packet_src, packet_src->dumb, packet_src->dumb,
        //     IPPacket{msg.to_buffer()}))
        //         SetJSONError("DNS query not accepted by endpoint", dnsquery.response);
        // }
        // else
        //     SetJSONError("Endpoint does not have dns", dnsquery.response);
        return;
    }
#endif

    void RPCServer::HandleLogsSubRequest(oxenmq::Message& m)
    {
        if (m.data.size() != 1)
        {
            m.send_reply("Invalid subscription request: no log receipt endpoint given");
            return;
        }

        if (!log_subs)
        {
            m.send_reply("This Session Router instance is not capturing logs");
            return;
        }

        auto endpoint = std::string{m.data[0]};

        if (endpoint == "unsubscribe")
        {
            log::debug(logcat, "New logs unsubscribe request from conn {}@{}", m.conn.to_string(), m.remote);
            log_subs->unsubscribe(m.conn);
            m.send_reply("OK");
            return;
        }

        auto is_new = log_subs->subscribe(m.conn, endpoint);

        if (is_new)
        {
            log::debug(logcat, "New logs subscription request from conn {}@{}", m.conn.to_string(), m.remote);
            m.send_reply("OK");
            log_subs->send_all(m.conn, endpoint);
        }
        else
        {
            log::debug(logcat, "Renewed logs subscription request from conn id {}@{}", m.conn.to_string(), m.remote);
            m.send_reply("ALREADY");
        }
    }

}  // namespace srouter::rpc
