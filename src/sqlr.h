#ifndef SQLR_H
#define SQLR_H

#include <string>

#include <json.hpp>

std::string replicate_sql(bool report, const std::string &db_name,
                          const jsonio::json &tables,
                          const jsonio::json &clients);

#endif // SQLR_H
