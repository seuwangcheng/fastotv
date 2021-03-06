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

#pragma once

#include <stdint.h>  // for uintmax_t

#include <functional>  // for function
#include <string>      // for string

#include "commands/commands.h"  // for cmd_seq_t

namespace fasto {
namespace fastotv {
namespace inner {
class InnerClient;
}
}  // namespace fastotv
}  // namespace fasto

namespace fasto {
namespace fastotv {
namespace inner {

class RequestCallback {
 public:
  typedef std::function<void(cmd_seq_t request_id, int argc, char* argv[])> callback_t;
  RequestCallback(cmd_seq_t request_id, callback_t cb);
  cmd_seq_t GetRequestID() const;
  void Execute(int argc, char* argv[]);

 private:
  cmd_seq_t request_id_;
  callback_t cb_;
};

class InnerServerCommandSeqParser {
 public:
  typedef uintmax_t id_t;

  InnerServerCommandSeqParser();
  virtual ~InnerServerCommandSeqParser();

  void SubscribeRequest(const RequestCallback& req);

 protected:
  void HandleInnerDataReceived(InnerClient* connection, const std::string& input_command);

  cmd_seq_t NextRequestID();  // for requests

 private:
  void ProcessRequest(cmd_seq_t request_id, int argc, char* argv[]);

  virtual void HandleInnerRequestCommand(InnerClient* connection,
                                         cmd_seq_t id,
                                         int argc,
                                         char* argv[]) = 0;  // called when argv not NULL and argc > 0 , only responce
  virtual void HandleInnerResponceCommand(
      InnerClient* connection,
      cmd_seq_t id,
      int argc,
      char* argv[]) = 0;  // called when argv not NULL and argc > 0, only approve responce
  virtual void HandleInnerApproveCommand(InnerClient* connection,
                                         cmd_seq_t id,
                                         int argc,
                                         char* argv[]) = 0;  // called when argv not NULL and argc > 0

  common::atomic<id_t> id_;
  std::vector<RequestCallback> subscribed_requests_;
};

}  // namespace inner
}  // namespace fastotv
}  // namespace fasto
