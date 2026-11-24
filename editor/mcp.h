// The editor's MCP server (Model Context Protocol, JSON-RPC 2.0). The tools call the same Doc
// functions as the UI buttons. Two transports: Streamable HTTP on 127.0.0.1 (POST /mcp, JSON
// responses, no SSE) and newline-delimited JSON-RPC on stdin/stdout (--mcp-stdio, headless).
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "doc.h"

// One JSON-RPC message or batch in, the response out. Empty string: nothing to send (a notification).
// post != nullptr (the HTTP transport): a slow tail of a call (test_run's wait_seconds) is not done here, on the thread that owns the
// Doc; instead *post is set to a function that completes the response string and may be run on any thread (it never touches the Doc).
std::string mcpHandleMessage(Doc &doc, const std::string &body, std::function<void(std::string &)> *post = nullptr);

// Reads one request per line from stdin, writes one response per line to stdout, until EOF.
int mcpStdioLoop(Doc &doc);

// Origin / Host check shared with the HTTP transport (exposed for the smoke test).
bool mcpOriginAllowed(const std::string &origin);

class McpHttp {
public:
    McpHttp();
    ~McpHttp();
    // Binds 127.0.0.1:port (tries the next ports when taken). `runOnMain` executes a job on the
    // thread that owns the Doc and returns when it has run.
    bool start(int port, std::function<void(std::function<void()>)> runOnMain, Doc &doc, std::string &err);
    void stop();
    int port() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
