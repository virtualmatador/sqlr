#ifndef SQLR_H
#define SQLR_H

#include <string>

#include <json.hpp>

std::string replicate_sql(const std::string &db_name,
                          const jsonio::json &tables,
                          const jsonio::json &clients, bool report,
                          bool dry_run);

#endif // SQLR_H
