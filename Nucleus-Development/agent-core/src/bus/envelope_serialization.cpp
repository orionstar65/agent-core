#include "agent/envelope_serialization.hpp"
#include "agent/envelope_json.hpp"

namespace agent {

    std::string serialize_envelope(const Envelope& envelope) {
        return envelope_json::serialize_envelope_template(envelope);
    }

    bool deserialize_envelope(const std::string& json_str, Envelope& envelope) {
        return envelope_json::deserialize_envelope_template(json_str, envelope);
    }

}
