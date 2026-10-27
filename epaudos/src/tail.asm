; SPDX-License-Identifier: GPL-3.0-or-later
; Dernier objet lié : les données C sont conservées avant la pile et la borne.
.8086
.model tiny
public stack_top, _resident_end
.data?
align 2
private_stack db 2048 dup (?)
stack_top label byte
_resident_end label byte
end
