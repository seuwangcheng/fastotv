/*  Copyright (C) 2014-2017 FastoGT. All right reserved.

    This file is part of FastoTV.

    FastoTV is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FastoTV is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FastoTV. If not, see <http://www.gnu.org/licenses/>.
*/

#include "server_info.h"

#include <string>

#include "third-party/json-c/json-c/json.h"  // for json_object_...

#include <common/convert2string.h>

namespace fasto {
namespace fastotv {

ServerInfo::ServerInfo() : bandwidth_host() {
}

struct json_object* ServerInfo::MakeJobject(const ServerInfo& inf) {
  json_object* obj = json_object_new_object();
  const std::string host_str = common::ConvertToString(inf.bandwidth_host);
  json_object_object_add(obj, BANDWIDTH_HOST_FIELD, json_object_new_string(host_str.c_str()));
  return obj;
}

ServerInfo ServerInfo::MakeClass(json_object* obj) {
  json_object* jband = NULL;
  json_bool jband_exists = json_object_object_get_ex(obj, BANDWIDTH_HOST_FIELD, &jband);
  ServerInfo inf;
  if (jband_exists) {
    const std::string host_str = json_object_get_string(jband);
    common::net::HostAndPort hs;
    if (common::ConvertFromString(host_str, &hs)) {
      inf.bandwidth_host = hs;
    }
  }
  return inf;
}

}  // namespace fastotv
}  // namespace fasto
