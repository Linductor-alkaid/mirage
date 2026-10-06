#include "../service/host.hpp"
// DEC-045: RuntimeService is the sole Executor owner inside this tray process.
int main(int argc, char **argv) { return mirage::apps::run_service_host(argc, argv, true); }
