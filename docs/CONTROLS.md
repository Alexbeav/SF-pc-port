# Keyboard and mouse controls

The launcher exposes all 31 native gameplay actions. Bindings are captured as
physical keyboard scancodes or mouse inputs and are saved in
`%LOCALAPPDATA%\SyphonFilterPC\launcher.ini`.

| Action | Default | Runtime meaning |
| --- | --- | --- |
| Move forward / backward | W / S | Walk in chase mode; crouch-walk while kneeling |
| Turn left / right | Q / E | Keyboard fallback for tank turning; horizontal mouse motion owns normal chase yaw |
| Strafe left / right | A / D | Lateral movement, manual-aim corner peek and side-roll direction |
| Run | Left Shift | Run while moving forward |
| Roll | Space | Forward roll, or left/right with a strafe direction; no backward roll |
| Reload | R | Reload when the current weapon can accept reserve ammunition |
| Aim | Mouse Right | Hold for manual first-person aim; forward/turn are locked, Strafe remains active |
| Fire | Mouse Left | Fire the current weapon |
| Crouch / stealth | C | Toggle kneeling; movement becomes stealth crouch-walk |
| Action / interact | F | Contextual doors, switches, pickups and mission actions |
| Target lock | Tab | Press to select/cycle; hold to retain automatic target lock |
| Quick turn | Backspace | Retail 180-degree turn |
| Quick weapon switch | Mouse Middle | Short Select-style weapon switch |
| Previous / next weapon | [ / ] | Move backward/forward through the owned retail weapon ring |
| Weapon menu previous / next | Wheel Down / Wheel Up | Move backward/forward through the owned retail weapon ring |
| Pause menu | Escape | Open or close pause |
| Quick weapon 1..10 | 1..9, 0 | Equip the corresponding owned retail weapon slot |
| SF2 quick save | F5 | Capture one process-local guest-machine state |
| SF2 quick load | F9 | Restore the most recent F5 state |

Horizontal mouse movement turns in chase mode. While Aim is held, both mouse
axes control the retail first-person sight instead. Crouch plus movement is the
stealth locomotion path; Roll plus Strafe selects a side roll. These are
composed states, not separate bindable actions.

The SF2 quick state is intentionally temporary: it is replaced by the next
F5 press and is discarded when the game closes. It exists for bring-up and
repeatable mission testing, not as a compatible on-disk save format.
