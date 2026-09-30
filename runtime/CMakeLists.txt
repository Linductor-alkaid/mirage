add_subdirectory(ipc)
add_subdirectory(mira_host)
add_subdirectory(persistence)
# The updater core (M5-12, DEC-032) precedes apps: the CLI links it.
add_subdirectory(update)
add_subdirectory(permission)
add_subdirectory(service)
