#ifndef SQLR_H
#define SQLR_H

#include <string>

#include <json.h>

std::string replicate_sql(
    const std::string_view name, const jsonio::json& definition);

#endif // SQLR_H
