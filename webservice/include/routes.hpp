#ifndef ROUTES_HPP
#define ROUTES_HPP

#include "httplib.h"

namespace webservice {

class PoolOrchestrator;

/**
 * @brief Register all HTTP routes with the server
 * @param server HTTP server instance
 * @param orchestrator Pool orchestrator for task submission
 */
void register_routes(httplib::Server& server, PoolOrchestrator& orchestrator);

} // namespace webservice

#endif // ROUTES_HPP
