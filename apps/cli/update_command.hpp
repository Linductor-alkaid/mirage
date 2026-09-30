// M5-12 (DEC-032): `mirage update check / apply` — CLI entry over the
// updater core (mirage_update); the tray / desktop shell adopt the same
// core for the in-app surface later.
#pragma once

int command_update_check(int argc, char **argv);
int command_update_apply(int argc, char **argv);
