#pragma once

#include "agent/bus.hpp"
#include <string>

namespace agent {

std::string serialize_envelope(const Envelope& envelope);

bool deserialize_envelope(const std::string& json_str, Envelope& envelope);

}
