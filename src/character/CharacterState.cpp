#include "character/CharacterState.h"

namespace deskpet::character {

std::string_view toString(State state) {
    switch (state) {
        case State::Idle: return "Idle";
        case State::Dragged: return "Dragged";
        case State::Airborne: return "Airborne";
        case State::Walk: return "Walk";
    }
    return "Unknown";
}

}  // namespace deskpet::character
