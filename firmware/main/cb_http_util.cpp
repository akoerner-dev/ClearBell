// cb_http_util.cpp - see cb_http_util.h.
#include "cb_http_util.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>

#include "cb_modes.h"
#include "cb_provisioning.h"
#include "cb_setup.h"

namespace {
int recv_fn(void *ctx, char *buf, size_t len) { return httpd_req_recv(static_cast<httpd_req_t *>(ctx), buf, len); }
uint32_t now_fn() { return millis(); }

// Header into buf ("" if missing or too long); returns buf or nullptr when missing.
const char *header(httpd_req_t *req, const char *name, char *buf, size_t cap) {
  size_t l = httpd_req_get_hdr_value_len(req, name);
  if (!l || l >= cap || httpd_req_get_hdr_value_str(req, name, buf, cap) != ESP_OK) return nullptr;
  return buf;
}
}  // namespace

cb::BodyReader cb_http_reader(httpd_req_t *req, uint32_t total_ms) {
  return cb::BodyReader(recv_fn, req, now_fn, total_ms);
}

bool cb_http_read_body(httpd_req_t *req, char *buf, size_t cap, uint32_t total_ms) {
  size_t len = req->content_len;
  if (len == 0 || len >= cap) return false;
  cb::BodyReader r = cb_http_reader(req, total_ms);
  if (!cb::read_full(r, buf, len)) return false;
  buf[len] = 0;
  return true;
}

uint32_t cb_http_peer_ipv4(httpd_req_t *req) {
  struct sockaddr_in6 a = {};
  socklen_t len = sizeof a;
  if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&a, &len) != 0) return 0;
  if (a.sin6_family == AF_INET6) return a.sin6_addr.un.u32_addr[3];  // v4-mapped
  return ((struct sockaddr_in *)&a)->sin_addr.s_addr;
}

cb::AccessInput cb_access_input(httpd_req_t *req) {
  const CbProvisioning &p = cb_prov();
  cb::AccessInput in;
  in.mode = cb_mode();
  in.from_ap = cb_setup_from_ap(req);
  in.provisioned = p.valid;
  in.flat_set = p.role == cb::UnitId::EG || p.role == cb::UnitId::OG;
  in.indoor_paired = p.has_key[(int)cb::UnitId::Outdoor];
  return in;
}

bool cb_http_origin_ok(httpd_req_t *req, bool via_ap) {
  const char *method = http_method_str((enum http_method)req->method);
  char origin[96], site[24], xcb[8], host[96], ip[16];
  snprintf(ip, sizeof ip, "%s", WiFi.localIP().toString().c_str());
  return cb::request_origin_ok(method, header(req, "Origin", origin, sizeof origin),
                               header(req, "Sec-Fetch-Site", site, sizeof site), header(req, "X-ClearBell", xcb, sizeof xcb),
                               header(req, "Host", host, sizeof host), cb_prov().serial, ip, via_ap);
}
