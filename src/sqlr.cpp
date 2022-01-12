#include <map>

#include "sqlr.h"

void sanitize(const std::string& input, const char* bad_chars)
{
    while(*bad_chars)
    {
        if (input.find(*bad_chars++) != std::string::npos)
        {
            throw std::runtime_error(input.c_str());
        }
    }
}

std::string replicate_sql(const std::string& db_name,
    const jsonio::json& definition, const jsonio::json& clients)
{
    std::string bad_prefix{ "_sql_" };
    std::map<std::string, std::size_t> table_ids;
    for (const auto& table : definition.get_array())
    {
        sanitize(table["name"].get_string(), "'`");
        if (table["name"].get_string().rfind(bad_prefix, 0) == 0)
        {
            throw std::runtime_error("Publish MySQL: Table Bad Prefix");
        }
        sanitize(table["id"].get_string(), "'`");
        if (++table_ids[table["id"].get_string()] > 1)
        {
            throw std::runtime_error("Publish MySQL: Repeated Table Id");
        }
        std::map<std::string, std::size_t> column_ids;
        for (const auto& column : table["columns"].get_array())
        {
            sanitize(column["name"].get_string(), "'`");
            if (column["name"].get_string().rfind(bad_prefix, 0) == 0)
            {
                throw std::runtime_error("Publish MySQL: Column Bad Prefix");
            }
            sanitize(column["type"].get_string(), "'`");
            sanitize(column["id"].get_string(), "'`");
            auto default_value = column.get_value("default");
            if (default_value)
            {
                sanitize(default_value->get_string(), "'`");
            }
            if (++column_ids[column["id"].get_string()] > 1)
            {
                throw std::runtime_error("Publish MySQL: Repeated Column Id");
            }
        }
        std::map<std::string, std::size_t> index_names;
        for (const auto& key : table["keys"].get_array())
        {
            if (key["columns"].get_array().size() == 0)
            {
                throw std::runtime_error("Publish MySQL: No Key Column");
            }
            for (const auto& clm : key["columns"].get_array())
            {
                sanitize(clm.get_string(), "'`");
            }
            if (key["type"].get_string() == "primary key")
            {
                if (++index_names[key["type"].get_string()] > 1)
                {
                    throw std::runtime_error(
                        "Publish MySQL: Repeated Primary Key");
                }
                if (key.get_value("name"))
                {
                    throw std::runtime_error(
                        "Publish MySQL: Primary Key With Name");
                }
            }
            else
            {
                sanitize(key["name"].get_string(), "'`");
                if (++index_names[key["name"].get_string()] > 1)
                {
                    throw std::runtime_error(
                        "Publish MySQL: Repeated Key Name");
                }
            }
        }
        auto foreign_keys = table.get_value("foreign-keys");
        if (foreign_keys)
        {
            for (const auto& foreign_key : foreign_keys->get_array())
            {
                sanitize(foreign_key["delete"].get_string(), "'`");
                sanitize(foreign_key["update"].get_string(), "'`");
                sanitize(foreign_key["table"].get_string(), "'`");
                if (foreign_key["columns"].get_array().size() == 0)
                {
                    throw std::runtime_error(
                        "Publish MySQL: No ForeignKey Column");
                }
                for (const auto& clm : foreign_key["columns"].get_array())
                {
                    sanitize(clm.get_string(), "'`");
                }
                if (foreign_key["keys"].get_array().size() == 0)
                {
                    throw std::runtime_error(
                        "Publish MySQL: No ForeignKey Key");
                }
                for (const auto& clm : foreign_key["keys"].get_array())
                {
                    sanitize(clm.get_string(), "'`");
                }
            }
        }
    }
    std::string exec = R"(
prepare stmt from @qry;
execute stmt;
deallocate prepare stmt;
)";

    // Start Transaction
    std::string sql_cmd = "";
    sql_cmd += R"(
set foreign_key_checks = 0;
)";

    // Create database
    sql_cmd += R"(
set @qry = 'CREATE DATABASE IF NOT EXISTS `)" +
    db_name + R"(`;';
)";
    sql_cmd += exec;

    // Apply tables name with prefix
    sql_cmd += R"(
set @all_tables = '';
)";
    for (const auto& table : definition.get_array())
    {
        sql_cmd += R"(
set @all_tables = concat(@all_tables, '`)" +
    table["id"].get_string() + R"(`');
set @old_table = '';
select `TABLE_NAME` into @old_table
    from `INFORMATION_SCHEMA`.`TABLES`
    where `TABLE_COMMENT` = ')" + table["id"].get_string() + R"(' and
        `TABLE_SCHEMA` = ')" + db_name + R"(';

set @qry = if (@old_table = '',
    'CREATE TABLE `)" + db_name + R"(`.`)" +
        bad_prefix + table["name"].get_string() + R"(` (`)" + bad_prefix +
        R"(` int UNSIGNED NOT NULL) ENGINE=)" + table["engine"].get_string() +
        R"( DEFAULT CHARSET=utf8 COMMENT \')" + table["id"].get_string() +
        R"(\';'
,
    concat('ALTER TABLE `)" + db_name +
        R"(`.`', @old_table, '` rename `)" + db_name +
        R"(`.`)" + bad_prefix + table["name"].get_string() + R"(`, ENGINE=)" +
        table["engine"].get_string() + R"(;')
);
)";
        sql_cmd += exec;
    }

    // Drop extra tables
    sql_cmd += R"(
select group_concat(concat('`)" + db_name +
    R"(`.`', `TABLE_NAME`, '`') SEPARATOR ', ')
    into @sub_query
    from `INFORMATION_SCHEMA`.`TABLES`
    where `TABLE_SCHEMA`= ')" + db_name +
    R"(' and `TABLE_TYPE` = 'BASE TABLE' and
        instr(@all_tables, concat('`', `TABLE_COMMENT`, '`')) = 0;
set @qry = if (isnull(@sub_query),
    'SELECT 0;'
,
    concat('DROP TABLE ', @sub_query, ';')
);
)";
    sql_cmd += exec;

    // Apply tables name
    for (const auto& table : definition.get_array())
    {
        sql_cmd += R"(
set @qry = 'ALTER TABLE `)" + db_name +
        R"(`.`)" + bad_prefix + table["name"].get_string() +
        R"(` rename `)" + db_name + R"(`.`)" +
        table["name"].get_string() + R"(`, ';
)";

        // Drop keys
        sql_cmd += R"(
select group_concat(distinct concat('DROP index `', `INDEX_NAME`, '`, ') SEPARATOR '')
    into @sub_query
    from `INFORMATION_SCHEMA`.`STATISTICS`
    inner join `INFORMATION_SCHEMA`.`TABLES`
    on `STATISTICS`.`TABLE_NAME` = `TABLES`.`TABLE_NAME` and
        `STATISTICS`.`TABLE_SCHEMA` = `TABLES`.`TABLE_SCHEMA`
    where `STATISTICS`.`TABLE_SCHEMA` = ')" + db_name +
        R"(' and `TABLE_COMMENT` = ')" + table["id"].get_string() + R"(';
set @qry = concat(@qry, ifnull(@sub_query, ''));
)";

        // Drop foreign keys
        sql_cmd += R"(
select group_concat(concat('DROP foreign key `', `CONSTRAINT_NAME`, '`, ')
    SEPARATOR '') into @sub_query
    from `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`
    inner join `INFORMATION_SCHEMA`.`TABLES`
    on `KEY_COLUMN_USAGE`.`TABLE_NAME` = `TABLES`.`TABLE_NAME` and
        `CONSTRAINT_SCHEMA` = `TABLES`.`TABLE_SCHEMA`
    where `CONSTRAINT_SCHEMA` = ')" + db_name +
        R"(' and `TABLE_COMMENT` = ')" + table["id"].get_string() +
        R"(' and `REFERENCED_TABLE_NAME` is not null;
set @qry = concat(@qry, ifnull(@sub_query, ''));
)";

        // Apply column properties
        sql_cmd += R"(
set @all_columns = '';
)";
        std::string pre_column = "first";
        for (const auto& column : table["columns"].get_array())
        {
            auto default_value = column.get_value("default");
            sql_cmd += R"(
set @all_columns = concat(@all_columns, '`)" +
    column["id"].get_string() + R"(`');
set @old_column = '';
select `COLUMN_NAME` into @old_column
    from `INFORMATION_SCHEMA`.`COLUMNS`
    inner join `INFORMATION_SCHEMA`.`TABLES`
    on `COLUMNS`.`TABLE_NAME` = `TABLES`.`TABLE_NAME` and
        `COLUMNS`.`TABLE_SCHEMA` = `TABLES`.`TABLE_SCHEMA`
    where `COLUMN_COMMENT` = ')" + column["id"].get_string() + R"(' and
        `TABLE_COMMENT` = ')" + table["id"].get_string() + R"(' and
        `COLUMNS`.`TABLE_SCHEMA` = ')" + db_name + R"(';

set @qry = if (@old_column = '',
    concat(@qry, 'add')
,
    concat(@qry, 'change `', @old_column, '`')
);
set @qry = concat(@qry, ' `)" + column["name"].get_string() +
    R"(` )" + column["type"].get_string() +
    (default_value ? " default " + default_value->get_string() : "") +
    (column["null"].get_bool() ? " null" : " not null") +
    (column["auto"].get_bool() ? " auto_increment" : "") +
    R"( COMMENT \')" + column["id"].get_string() +
    R"(\' )" + pre_column + R"(, ');
)";
            pre_column = "after `" + column["name"].get_string() + "`";
        }
        // Remove Extra Columns
        sql_cmd += R"(
select group_concat(concat('DROP `', `COLUMN_NAME`, '`, ') SEPARATOR '')
    into @sub_query
    from `INFORMATION_SCHEMA`.`COLUMNS`
    inner join `INFORMATION_SCHEMA`.`TABLES`
    on `COLUMNS`.`TABLE_NAME` = `TABLES`.`TABLE_NAME` and
        `COLUMNS`.`TABLE_SCHEMA` = `TABLES`.`TABLE_SCHEMA`
    where `TABLE_COMMENT` = ')" + table["id"].get_string() + R"(' and
        `COLUMNS`.`TABLE_SCHEMA` = ')" + db_name + R"(' and
        (
            `COLUMN_COMMENT` = '' or
            instr(@all_columns, concat('`', `COLUMN_COMMENT`, '`')) = 0
        );
set @qry = concat(@qry, ifnull(@sub_query, ''));
)";
        auto keys = table.get_value("keys");
        if (keys)
        {
            // Add keys
            for (const auto& key : keys->get_array())
            {
                std::string key_name;
                auto key_name_value = key.get_value("name");
                if (key_name_value)
                {
                    key_name = " `" + key_name_value->get_string() + "`";
                }
                std::string key_def;
                for (const auto& clm : key["columns"].get_array())
                {
                    if (!key_def.empty())
                    {
                        key_def += ", ";
                    }
                    key_def += '`' + clm.get_string() + '`';
                }
                sql_cmd += R"(
set @qry = concat(@qry, 'add )" + key["type"].get_string() +
    key_name + R"( ()" + key_def + R"(), ');
)";
            }
        }
        sql_cmd += R"(
set @qry = concat(substr(@qry, 1, length(@qry) - 2), ';');
)";
    sql_cmd += exec;
    }

    // Apply foreign keys
    sql_cmd += R"(
set foreign_key_checks = 1;
)";
    for (const auto& table : definition.get_array())
    {
        auto foreign_keys = table.get_value("foreign-keys");
        if (foreign_keys && foreign_keys->get_array().size() > 0)
        {
            sql_cmd += R"(
set @qry = 'ALTER TABLE `)" + db_name +
    R"(`.`)" + table["name"].get_string() + R"(` ';
)";
            for (const auto& key : foreign_keys->get_array())
            {
                std::string key_def;
                for (const auto& clm : key["columns"].get_array())
                {
                    if (!key_def.empty())
                    {
                        key_def += ", ";
                    }
                    key_def += '`' + clm.get_string() + '`';
                }
                std::string f_key_def;
                for (const auto& f_key : key["keys"].get_array())
                {
                    if (!f_key_def.empty())
                    {
                        f_key_def += ", ";
                    }
                    f_key_def += '`' + f_key.get_string() + '`';
                }
                sql_cmd += R"(
set @qry = concat(@qry, 'ADD FOREIGN KEY ()" + key_def + R"() REFERENCES `)" +
    db_name + R"(`.`)" + key["table"].get_string() +
    R"(` ()" + f_key_def + R"() ON DELETE )" +
    key["delete"].get_string() + R"( ON UPDATE )" +
    key["update"].get_string() + R"(, ');
)";
            }
            sql_cmd += R"(
set @qry = concat(substr(@qry, 1, length(@qry) - 2), ';');
)";
        sql_cmd += exec;
        }
    }

    // Apply users
    std::size_t index = 0;
    for (const auto& client : clients.get_array())
    {
        const std::string& user = client["user"].get_string();
        sql_cmd += R"(
set @client_user = ')" + user + R"(';
set @qry = concat('DROP USER IF EXISTS \'', @client_user, '\';');
)";
        sql_cmd += exec;
        sql_cmd += R"(
set @letters = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ~!@#$%^&*()-_=+|/?,.<>{}[]';
select convert(concat(
)";
        for (std::size_t i = 0; i < 20; ++i)
        {
            sql_cmd += R"(
    substring(@letters, floor(rand() * length(@letters)), 1),
)";
        }
        sql_cmd += R"(
    ''), NCHAR) into @client_password;
set @qry = concat('CREATE USER \'', @client_user,
    '\' IDENTIFIED BY \'', @client_password, '\';');
)";
        sql_cmd += exec;
        for (const auto& permission : client["permissions"].get_array())
        {
            std::string operations;
            for (const auto& operation : permission["operations"].get_array())
            {
                if (!operations.empty())
                {
                    operations += ", ";
                }
                operations += operation.get_string();
            }
            sql_cmd += R"(
set @qry = 'GRANT )" + operations + R"( ON `)" +
    db_name + R"(`.`)" +
    permission["subject"].get_string() + R"(` TO \')" +
    user + R"(\';';
)";
            sql_cmd += exec;
        }
        sql_cmd += R"(
set @qry = concat('select \'', @client_password, '\' into OUTFILE \'',
    @@global.secure_file_priv, @client_user, '\';');
)";
        sql_cmd += exec;
    }
    return sql_cmd;
}
