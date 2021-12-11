#ifndef SQLR_H
#define SQLR_H

#include <string>

#include <json.h>

std::string replicate_sql(const std::string& db_name,
    const jsonio::json& definition, const jsonio::json& clients);

#endif // SQLR_H
