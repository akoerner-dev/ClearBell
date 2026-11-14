// cb_http_util.h - device glue for the web-server protections of cb_http_guard.h (VK-06_Webserver_Robustheit).
#pragma once

#include <esp_http_server.h>

#include <cstddef>
#include <cstdint>

#include "cb_http_guard.h"

constexpr uint32_t kHttpFormMs = 15000;  // a small form body must be complete within this

// A reader for the request body: two 10-s receive timeouts in a row or total_ms end it (RV-17).
cb::BodyReader cb_http_reader(httpd_req_t *req, uint32_t total_ms);
// The whole body (content_len < cap) into buf, NUL-terminated. False: empty, too long, incomplete or too slow.
bool cb_http_read_body(httpd_req_t *req, char *buf, size_t cap, uint32_t total_ms = kHttpFormMs);
// IPv4 of the client (lwIP byte order), 0 if unknown.
uint32_t cb_http_peer_ipv4(httpd_req_t *req);
// RV-39: may this request change something? Reads Origin, Sec-Fetch-Site, X-ClearBell and Host.
bool cb_http_origin_ok(httpd_req_t *req, bool via_ap);
