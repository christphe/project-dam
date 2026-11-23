// Streamable HTTP transport of the MCP server: POST /mcp on 127.0.0.1, JSON in, JSON out.
// (cpp-httplib is vendored in third_party/; this is the only file that includes it.)
// cpp-httplib needs Windows 10; the root build defines _WIN32_WINNT for Windows 7.
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "mcp.h"

#include <thread>

#include "httplib.h"

struct McpHttp::Impl {
    httplib::Server svr;
    std::thread th;
    int port = 0;
};

McpHttp::McpHttp() : m(new Impl) {}
McpHttp::~McpHttp() { stop(); }

int McpHttp::port() const { return m->port; }

static bool hostAllowed(const std::string &host)   // the Host header: "127.0.0.1:8765", "localhost:8765"
{
    return mcpOriginAllowed("http://" + host);
}

bool McpHttp::start(int port, std::function<void(std::function<void()>)> runOnMain, Doc &doc, std::string &err)
{
    Doc *docp = &doc;
    m->svr.Post("/mcp", [docp, runOnMain](const httplib::Request &req, httplib::Response &res) {
        // DNS-rebinding guard: only requests that name us as localhost, and no foreign Origin.
        if (!mcpOriginAllowed(req.get_header_value("Origin")) || !hostAllowed(req.get_header_value("Host"))) {
            res.status = 403;
            res.set_content("{\"error\":\"forbidden origin\"}", "application/json");
            return;
        }
        std::string out;
        runOnMain([&] { out = mcpHandleMessage(*docp, req.body); });
        if (out.empty()) { res.status = 202; return; }   // notification: accepted, no body
        res.set_content(out, "application/json");
    });
    auto notAllowed = [](const httplib::Request &, httplib::Response &res) {
        res.status = 405;
        res.set_header("Allow", "POST");
        res.set_content("{\"error\":\"POST only\"}", "application/json");
    };
    m->svr.Get("/mcp", notAllowed);
    m->svr.Delete("/mcp", notAllowed);

    for (int p = port; p < port + 20; p++) {
        if (m->svr.bind_to_port("127.0.0.1", p)) { m->port = p; break; }
    }
    if (!m->port) { err = "cannot bind 127.0.0.1:" + std::to_string(port) + " (nor the next 19 ports)"; return false; }
    m->th = std::thread([this] { m->svr.listen_after_bind(); });
    return true;
}

void McpHttp::stop()
{
    if (!m) return;
    m->svr.stop();
    if (m->th.joinable()) m->th.join();
}
