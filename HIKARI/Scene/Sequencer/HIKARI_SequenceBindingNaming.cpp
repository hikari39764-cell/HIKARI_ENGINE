#include "Scene/Sequencer/HIKARI_SequenceBindingNaming.h"

#include <cctype>

namespace HIKARI::SEQUENCER {

    std::string NormalizeSequenceBindingSlotName(
        std::string value) {

        for (char& character : value) {
            const unsigned char byte =
                static_cast<unsigned char>(character);
            if (!std::isalnum(byte) &&
                character != '_' &&
                character != '.') {

                character = '_';
            }
        }

        while (!value.empty() && value.back() == '_') {
            value.pop_back();
        }
        return value.empty()
            ? std::string("Camera")
            : value;
    }

} // namespace HIKARI::SEQUENCER
