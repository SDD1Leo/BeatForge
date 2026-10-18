// The request/response format shared by every transport (see IpcServer.h).
#include "beatforge/ipc/IpcServer.h"


namespace beatforge::ipc
{
    using Json = nlohmann::json;

    std::string handleRequestLine (const std::string& line, const std::function<Json (const std::string&, const Json&)>& run)
    {
        Json response;
        try
        {
            const auto request = Json::parse (line);
            if (! request.is_object())
                throw std::invalid_argument ("the request must be a JSON object");
            const auto id = request.contains ("id") ? request["id"] : Json (nullptr);
            if (! request.contains ("op") || ! request["op"].is_string())
                response = { { "ok", false }, { "error", { { "code", "bad_request" }, { "message", "'op' (a string) is required" } } } };
            else
                response = run (request["op"].get<std::string>(), request.contains ("args") ? request["args"] : Json::object());
            response["id"] = id;
        }
        catch (const Json::parse_error&)
        {
            response = { { "id", nullptr }, { "ok", false }, { "error", { { "code", "bad_request" }, { "message", "not valid JSON" } } } };
        }
        catch (const std::exception& e)
        {
            response = { { "id", nullptr }, { "ok", false }, { "error", { { "code", "bad_request" }, { "message", e.what() } } } };
        }
        return response.dump (-1, ' ', false, Json::error_handler_t::replace);
    }
}
