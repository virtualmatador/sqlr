#include <map>
#include <sstream>

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

std::string replicate_sql(bool report, const std::string& db_name,
    const jsonio::json& definition, const jsonio::json& clients)
{
    std::string bad_prefix{ "_sql_" };
    std::string drop_prefix{ "_drop_" };
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
        auto keys = table.get_value("keys");
        if (keys)
        {
            std::map<std::string, std::size_t> index_names;
            for (const auto& key : keys->get_array())
            {
                if (key["columns"].get_array().size() == 0)
                {
                    throw std::runtime_error("Publish MySQL: No Key Column");
                }
                for (const auto& clm : key["columns"].get_array())
                {
                    sanitize(clm.get_string(), "'`");
                }
                sanitize(key["name"].get_string(), "'`");
                if (++index_names[key["name"].get_string()] > 1)
                {
                    throw std::runtime_error(
                        "Publish MySQL: Repeated Key Name");
                }
                if (key["type"].get_string() == "primary key" &&
                    key["name"].get_string() != "PRIMARY")
                {
                    throw std::runtime_error(
                        "Publish MySQL: Invalid Primary Key Name");
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
    std::string exec;
    if (report)
    {
        exec += R"(
select @qry as '';
)";
    }
    exec += R"(
prepare stmt from @qry;
execute stmt;
deallocate prepare stmt;
)";

    // Start Transaction
    std::string sql = "";

    // Create database
    sql += R"(
set @old_db = null;
select `SCHEMA_NAME` into @old_db from `INFORMATION_SCHEMA`.`SCHEMATA`
where `SCHEMA_NAME` = ')" + db_name + R"(';
set @qry = if (isnull(@old_db),
    'CREATE DATABASE IF NOT EXISTS `)" + db_name + R"(`;'
,
    'SET @r = \'Database ")" + db_name + R"(" exists.\';'
);
)";
    sql += exec;

    // Create tables with prefix
    sql += R"(
set @all_tables = '';
)";
    for (const auto& table : definition.get_array())
    {
        sql += R"(
set @all_tables = concat(@all_tables, '{)" + table["id"].get_string() + R"(}');
set @old_table = null;
select `TABLE_NAME` into @old_table
    from `INFORMATION_SCHEMA`.`TABLES`
    where `TABLE_COMMENT` = ')" + table["id"].get_string() + R"(' and
        `TABLE_SCHEMA` = ')" + db_name + R"(';
set @qry = if (isnull(@old_table),
    'CREATE TABLE `)" + db_name + R"(`.`)" +
        bad_prefix + table["name"].get_string() + R"(` (`)" + bad_prefix +
        R"(` int UNSIGNED NOT NULL) ENGINE=)" + table["engine"].get_string() +
        R"( DEFAULT CHARSET=utf8 COMMENT \')" + table["id"].get_string() +
        R"(\';'
,
    'SET @r = \'Table ")" + table["name"].get_string() + R"(" exist.\';'
);
)";
        sql += exec;
    }

    // Mark extra tables
    sql += R"(
set @sub_query = null;
select group_concat(concat('`)" + db_name + R"(`.`', `TABLE_NAME`, '` to `)" +
    db_name + R"(`.`)" + bad_prefix + drop_prefix +
    R"(', `TABLE_NAME`, '`') SEPARATOR ', ')
    into @sub_query
    from `INFORMATION_SCHEMA`.`TABLES`
    where `TABLE_SCHEMA` = ')" + db_name +
    R"(' and `TABLE_TYPE` = 'BASE TABLE' and
        instr(@all_tables, concat('{', `TABLE_COMMENT`, '}')) = 0;
set @qry = if (isnull(@sub_query),
    'SET @r = \'No extra table.\';'
,
    concat('RENAME TABLE ', @sub_query, ';')
);
)";
    sql += exec;

    // Apply table names
    sql += R"(
set @rename_tables_prefix = '';
set @rename_tables_final = '';
)";
    for (const auto& table : definition.get_array())
    {
        sql += R"(
set @old_table = null;
select `TABLE_NAME` into @old_table
    from `INFORMATION_SCHEMA`.`TABLES`
    where `TABLE_COMMENT` = ')" + table["id"].get_string() + R"(' and
        `TABLE_SCHEMA` = ')" + db_name + R"(';
set @rename_tables_prefix = if (@old_table != ')" + table["name"].get_string() +
    R"(' && instr(@old_table, ')" + bad_prefix + R"(') != 1,
    concat(@rename_tables_prefix, '`)" + db_name +
        R"(`.`', @old_table, '` to `)" + db_name +
        R"(`.`)" + bad_prefix + table["name"].get_string() + R"(`, ')
,
    @rename_tables_prefix
);
set @rename_tables_final = if (@old_table != ')" + table["name"].get_string() +
    R"(',
    concat(@rename_tables_final, '`)" + db_name + R"(`.`)" + bad_prefix +
        table["name"].get_string() + R"(` to `)" + db_name + R"(`.`)" +
        table["name"].get_string() + R"(`, ')
,
    @rename_tables_final
);
)";
    }
    sql += R"(
set @qry = if (@rename_tables_final != '',
    if (@rename_tables_prefix != '', concat ('RENAME TABLE ',
        substr(@rename_tables_prefix, 1, length(@rename_tables_prefix) - 2), ';')
    ,
        'SET @r = \'All tables have prefix.\';'
    ),
    'SET @r = \'No table needs prefix.\';'
);
)";
    sql += exec;
    sql += R"(
set @qry = if (@rename_tables_final != '', concat ('RENAME TABLE ',
    substr(@rename_tables_final, 1, length(@rename_tables_final) - 2), ';')
,
    'SET @r = \'No table rename needed.\';');
)";
    sql += exec;

    // Apply table engine
    for (const auto& table : definition.get_array())
    {
        sql += R"(
set @old_engine = null;
select `ENGINE` into @old_engine
    from `INFORMATION_SCHEMA`.`TABLES`
    where `TABLE_NAME` = ')" + table["name"].get_string() + R"(' and
        `TABLE_SCHEMA` = ')" + db_name + R"(';
set @qry = if (@old_engine != ')" + table["engine"].get_string() + R"(',
    'ALTER TABLE `)" + db_name + R"(`.`)" + table["name"].get_string() +
        R"(` ENGINE=)" + table["engine"].get_string() + R"(;'
,
    'SET @r = \'Engine of ")" + table["name"].get_string() + R"(" is ok.\';'
);
)";
    sql += exec;
    }

    for (const auto& table : definition.get_array())
    {

        // Create columns with prefix
        sql += R"(
set @all_columns = '';
set @sub_query = '';
)";
        for (const auto& column : table["columns"].get_array())
        {
            sql += R"(
set @all_columns = concat(@all_columns, '{)" + column["id"].get_string() +
    R"(}');
set @old_column = null;
select `COLUMN_NAME` into @old_column
    from `INFORMATION_SCHEMA`.`COLUMNS`
    where `COLUMN_COMMENT` = ')" + column["id"].get_string() + R"(' and
        `COLUMNS`.`TABLE_NAME` = ')" + table["name"].get_string() + R"(' and
        `COLUMNS`.`TABLE_SCHEMA` = ')" + db_name + R"(';
set @sub_query = if (isnull(@old_column),
    concat(@sub_query, 'ADD `)" + bad_prefix + column["name"].get_string() +
        R"(` int unsigned COMMENT \')" + column["id"].get_string() + R"(\', ')
,
    @sub_query
);
)";
        }
        sql += R"(
set @qry = if (@sub_query != '',
    concat('ALTER TABLE `)" + db_name + R"(`.`)" + table["name"].get_string() +
        R"(` ', substr(@sub_query, 1, length(@sub_query) - 2), ';')
,
    'SET @r = \'No new column in ")" + table["name"].get_string() +
        R"(" is needed.\';'
);
)";
        sql += exec;

        // Mark Extra columns
        sql += R"(
set @sub_query = null;
select group_concat(concat('RENAME COLUMN `', `COLUMN_NAME`, '` to `)" +
    bad_prefix + drop_prefix + R"(', `COLUMN_NAME`, '`') SEPARATOR ', ')
    into @sub_query
    from `INFORMATION_SCHEMA`.`COLUMNS`
    where `TABLE_SCHEMA` = ')" + db_name +
    R"(' and `TABLE_NAME` = ')" + table["name"].get_string() + R"(' and
        instr(@all_columns, concat('{', `COLUMN_COMMENT`, '}')) = 0;
set @qry = if (isnull(@sub_query),
    'SET @r = \'No extra column in ")" + table["name"].get_string() + R"(".\';'
,
    concat('ALTER TABLE `)" + db_name + R"(`.`)" + table["name"].get_string() +
        R"(` ', @sub_query, ';')
);
)";
        sql += exec;

        // Apply column names
        sql += R"(
set @rename_columns_prefix = '';
set @rename_columns_final = '';
)";
        for (const auto& column : table["columns"].get_array())
        {
            sql += R"(
set @old_column = null;
select `COLUMN_NAME` into @old_column
    from `INFORMATION_SCHEMA`.`COLUMNS`
    where `COLUMN_COMMENT` = ')" + column["id"].get_string() + R"(' and
        `COLUMNS`.`TABLE_NAME` = ')" + table["name"].get_string() + R"(' and
        `COLUMNS`.`TABLE_SCHEMA` = ')" + db_name + R"(';
set @rename_columns_prefix = if (@old_column != ')" +
    column["name"].get_string() +
    R"(' && instr(@old_column, ')" + bad_prefix + R"(') != 1,
    concat(@rename_columns_prefix, 'RENAME COLUMN `', @old_column, '` to `)" +
        bad_prefix + column["name"].get_string() + R"(`, ')
,
    @rename_columns_prefix
);
set @rename_columns_final = if (@old_column != ')" +
    column["name"].get_string() + R"(',
    concat(@rename_columns_final, 'RENAME COLUMN `)" + bad_prefix +
        column["name"].get_string() + R"(` to `)" +
        column["name"].get_string() + R"(`, ')
,
    @rename_columns_final
);
)";
        }
        sql += R"(
set @qry = if (@rename_columns_final != '',
    if (@rename_columns_prefix != '',
        concat ('ALTER TABLE `)" + db_name + R"(`.`)" +
        table["name"].get_string() + R"(` ', substr(@rename_columns_prefix, 1,
        length(@rename_columns_prefix) - 2), ';')
    ,
        'SET @r = \'All columns in ")" + table["name"].get_string() +
            R"(" have prefix.\';'
    ),
    'SET @r = \'No column in ")" + table["name"].get_string() +
            R"(" needs prefix.\';'
);
)";
        sql += exec;
        sql += R"(
set @qry = if (@rename_columns_final != '', concat ('ALTER TABLE `)" + db_name +
        R"(`.`)" + table["name"].get_string() + R"(` ',
    substr(@rename_columns_final, 1, length(@rename_columns_final) - 2), ';')
,
    'SET @r = \'No column in ")" + table["name"].get_string() +
            R"(" needs rename.\';');
)";
        sql += exec;
    }

    // Drop wrong foreign keys
    std::map<std::string,
        std::map<std::string,
            std::pair<std::string, std::string>>> fk_flatten_columns;
    for (const auto& table : definition.get_array())
    {
        sql += R"(
set @all_foreign_keys = '';
)";
        auto foreign_keys = table.get_value("foreign-keys");
        if (foreign_keys)
        {
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
                fk_flatten_columns
                    [table["name"].get_string()][key["name"].get_string()] =
                        {std::move(key_def), std::move(f_key_def)};
                sql += R"(
set @all_foreign_keys = concat(@all_foreign_keys, ')" +
    key["name"].get_string() + R"( ');
set @old_constraint = null;
set @old_table = null;
set @old_key_def = null;
set @old_referenced_table = null;
set @old_f_key_def = null;
set @old_update_rule = null;
set @old_delete_rule = null;
select
    `fk`.`CONSTRAINT_NAME`,
    `fk`.`TABLE_NAME`,
    `fk`.`key_def`,
    `fk`.`REFERENCED_TABLE_NAME`,
    `fk`.`f_key_def`,
    `rk`.`UPDATE_RULE`,
    `rk`.`DELETE_RULE`
into
    @old_constraint,
    @old_table,
    @old_key_def,
    @old_referenced_table,
    @old_f_key_def,
    @old_update_rule,
    @old_delete_rule
from `INFORMATION_SCHEMA`.`REFERENTIAL_CONSTRAINTS` as `rk`
join (
select
    `CONSTRAINT_NAME`,
    `CONSTRAINT_SCHEMA`,
    `TABLE_NAME`,
    group_concat(concat('`', `COLUMN_NAME`, '`')
        ORDER BY `ORDINAL_POSITION`
        SEPARATOR ', ') as `key_def`,
    `REFERENCED_TABLE_NAME`,
    group_concat(concat('`', `REFERENCED_COLUMN_NAME`, '`')
        ORDER BY `POSITION_IN_UNIQUE_CONSTRAINT`
        SEPARATOR ', ') as `f_key_def`
from `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`
where
    `REFERENCED_TABLE_NAME` is not null and
    `CONSTRAINT_SCHEMA` = ')" + db_name + R"(' and
    `CONSTRAINT_NAME` = ')" + key["name"].get_string() + R"('
group by `CONSTRAINT_NAME`, `TABLE_NAME`, `REFERENCED_TABLE_NAME`) as `fk`
using (
    `CONSTRAINT_SCHEMA`,
    `CONSTRAINT_NAME`,
    `TABLE_NAME`,
    `REFERENCED_TABLE_NAME`);
set @old_ok = 
    @old_table = ')" + table["name"].get_string() + R"(' and
    @old_key_def = ')" + fk_flatten_columns
    [table["name"].get_string()][key["name"].get_string()].first + R"(' and
    @old_referenced_table = ')" + key["table"].get_string() + R"(' and
    @old_f_key_def = ')" + fk_flatten_columns
    [table["name"].get_string()][key["name"].get_string()].second + R"(' and
    @old_update_rule = ')" + key["update"].get_string() + R"(' and
    @old_delete_rule = ')" + key["delete"].get_string() + R"(';
set @qry = if (@old_ok or isnull(@old_constraint),
    'SET @r = \'Foreign key ")" + key["name"].get_string() +
        R"(" does not exist.\';'
,
    concat('ALTER TABLE `)" + db_name +
        R"(`.`', @old_table, '` DROP FOREIGN KEY `)" +
        key["name"].get_string() + R"(`;'));
)";
                sql += exec;
            }
        }

        // Remove extra foreign keys
        sql += R"(
set @sub_query = null;
select distinct group_concat(
    concat('DROP FOREIGN KEY `', `CONSTRAINT_NAME`, '`') SEPARATOR ', ')
into @sub_query
from `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`
where
    `REFERENCED_TABLE_NAME` is not null and
    `TABLE_SCHEMA` = ')" + db_name + R"(' and
    `TABLE_NAME` = ')" + table["name"].get_string() + R"(' and
    instr(@all_foreign_keys, `CONSTRAINT_NAME`) = 0;
set @qry = if (isnull(@sub_query),
    'SET @r = \'No extra foreign keys in ")" + table["name"].get_string() +
        R"(".\';'
,
    concat('ALTER TABLE `)" + db_name + R"(`.`)" + table["name"].get_string() +
        R"(` ', @sub_query, ';')
);
)";
        sql += exec;
    }


    for (const auto& table : definition.get_array())
    {
        // Apply column properties
        sql += R"(
set @sub_query = '';
set @ordinal_change = false;
)";
        std::string order = "FIRST";
        for (const auto& column : table["columns"].get_array())
        {
            std::ostringstream ordinal_position;
            ordinal_position << 1 +
                std::distance(&table["columns"].get_array().front(), &column);
            sql += R"(
set @old_type = null;
set @old_null = null;
set @old_auto = null;
set @old_position = null;
select `COLUMN_TYPE`, `IS_NULLABLE`, `EXTRA` like '%auto_increment%' as AUTO,
    `ORDINAL_POSITION`
    into @old_type, @old_null, @old_auto, @old_position
    from `INFORMATION_SCHEMA`.`COLUMNS`
    where `COLUMN_NAME` = ')" + column["name"].get_string() + R"(' and
        `COLUMNS`.`TABLE_NAME` = ')" + table["name"].get_string() + R"(' and
        `COLUMNS`.`TABLE_SCHEMA` = ')" + db_name + R"(';
set @ordinal_change = if (@old_position != )" + ordinal_position.str() +
    R"(, true, @ordinal_change);
set @sub_query = if (@ordinal_change or
    @old_type != ')" + column["type"].get_string() + R"(' or
    @old_null != ')" + (column["null"].get_bool() ? "YES" : "NO") + R"(' or
    @old_auto != )" + (column["auto"].get_bool() ? "true" : "false") + R"(,
    concat(@sub_query, 'MODIFY `)" + column["name"].get_string() + R"(` )" +
        column["type"].get_string() +
        (column["null"].get_bool() ? " null" : " not null") +
        (column["auto"].get_bool() ? " auto_increment" : "") +
        R"( COMMENT \')" + column["id"].get_string() + R"(\' )" +
        order + R"(, ')
,
    @sub_query
);
)";
        order = "AFTER `" + column["name"].get_string() + "`";
        }

        // Apply keys
        sql += R"(
set @all_keys = '';
)";
        auto keys = table.get_value("keys");
        if (keys)
        {
            for (const auto& key : keys->get_array())
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
                sql += R"(
set @all_keys = concat(@all_keys, ')" + key["name"].get_string() + R"( ');
set @old_index = null;
set @old_key_def = null;
select
    `INDEX_NAME`,
    group_concat(concat('`', `COLUMN_NAME`, '`')
        ORDER BY `SEQ_IN_INDEX` SEPARATOR ', ')
into
    @old_index,
    @old_key_def
from `INFORMATION_SCHEMA`.`STATISTICS`
where
    `TABLE_SCHEMA` = ')" + db_name + R"(' and
    `TABLE_NAME` = ')" + table["name"].get_string() + R"(' and
    `INDEX_NAME` = ')" + key["name"].get_string() + R"('
group by `INDEX_NAME`;
set @old_ok = @old_key_def = ')" + key_def + R"(';
set @drop_query = if (@old_ok or isnull(@old_index), '',
    'DROP INDEX `)" + key["name"].get_string() + R"(`, ');
set @sub_query = concat(@sub_query, @drop_query);
set @sub_query = if (@drop_query != '' or isnull(@old_index),
    concat(@sub_query, 'ADD )" + key["type"].get_string() + R"( `)" +
        key["name"].get_string() + R"(` ()" + key_def + R"(), ')
, @sub_query);
)";
            }
        }

        // Remove extra keys
        sql += R"(
set @drop_query = null;
select distinct group_concat(
    concat('DROP INDEX `', `INDEX_NAME`, '`') SEPARATOR ', ')
into @drop_query
from `INFORMATION_SCHEMA`.`STATISTICS`
join `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`
on
    `INFORMATION_SCHEMA`.`STATISTICS`.`INDEX_SCHEMA` =
    `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`.`CONSTRAINT_SCHEMA` and
    `INFORMATION_SCHEMA`.`STATISTICS`.`TABLE_NAME` =
    `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`.`TABLE_NAME` and
    `INFORMATION_SCHEMA`.`STATISTICS`.`INDEX_NAME` =
    `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`.`CONSTRAINT_NAME`
where
    `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`.`REFERENCED_TABLE_NAME` is null and
    `INFORMATION_SCHEMA`.`STATISTICS`.`INDEX_SCHEMA` = ')" + db_name + R"(' and
    `INFORMATION_SCHEMA`.`STATISTICS`.`TABLE_NAME` = ')" +
        table["name"].get_string() + R"(' and
    instr(@all_keys, `INDEX_NAME`) = 0;
set @sub_query = if (isnull(@drop_query), @sub_query,
    concat(@sub_query, @drop_query, ', ')
);
)";
        // Remove extra columns
        sql += R"(
set @drop_query = null;
select group_concat(concat('DROP COLUMN `', `COLUMN_NAME`, '`')
    SEPARATOR ', ') into @drop_query
    from `INFORMATION_SCHEMA`.`COLUMNS`
    where
        `COLUMNS`.`TABLE_NAME` = ')" + table["name"].get_string() + R"(' and
        `COLUMNS`.`TABLE_SCHEMA` = ')" + db_name + R"(' and
        `COLUMN_NAME` like ')" + bad_prefix + drop_prefix + R"(%';
set @sub_query = if (isnull(@drop_query), @sub_query,
    concat(@sub_query, @drop_query, ', ')
);
)";
        sql += R"(
set @qry = if (@sub_query != '',
    concat ('ALTER TABLE `)" + db_name + R"(`.`)" + table["name"].get_string() +
        R"(` ', substr(@sub_query, 1, length(@sub_query) - 2), ';')
,
    'SET @r = \'Table ")" + table["name"].get_string() + R"(" is ok.\';'
);
)";
        sql += exec;
    }

    // Remove extra tables
    sql += R"(
set @sub_query = null;
select group_concat(concat('`)" + db_name + R"(`.`', `TABLE_NAME`, '`')
    SEPARATOR ', ') into @sub_query
from `INFORMATION_SCHEMA`.`TABLES`
where
    `TABLE_SCHEMA` = ')" + db_name + R"(' and
    `TABLE_NAME` like ')" + bad_prefix + drop_prefix + R"(%';
set @qry = if (isnull(@sub_query), 'SET @r = \'No extra table.\';',
    concat('DROP TABLE ', @sub_query, ';')
);
)";
    sql += exec;

    // Create foreign keys
    for (const auto& table : definition.get_array())
    {
        auto foreign_keys = table.get_value("foreign-keys");
        if (foreign_keys)
        {
            for (const auto& key : foreign_keys->get_array())
            {
                sql += R"(
set @old_constraint = null;
set @old_table = null;
set @old_key_def = null;
set @old_referenced_table = null;
set @old_f_key_def = null;
select `CONSTRAINT_NAME` into @old_constraint
from `INFORMATION_SCHEMA`.`KEY_COLUMN_USAGE`
where
    `REFERENCED_TABLE_NAME` is not null and
    `TABLE_SCHEMA` = ')" + db_name + R"(' and
    `CONSTRAINT_NAME` = ')" + key["name"].get_string() + R"('
group by `CONSTRAINT_NAME`;
set @create_query = if (isnull(@old_constraint),
    concat('ALTER TABLE `)" + db_name + R"(`.`)" +
        table["name"].get_string() + R"(` ADD CONSTRAINT `)" +
        key["name"].get_string() + R"(` FOREIGN KEY ()" + fk_flatten_columns
        [table["name"].get_string()][key["name"].get_string()].first +
        R"() REFERENCES `)" + db_name + R"(`.`)" +
        key["table"].get_string() + R"(` ()" + fk_flatten_columns
        [table["name"].get_string()][key["name"].get_string()].second +
        R"() ON UPDATE )" + key["update"].get_string() +
        R"( ON DELETE )" + key["delete"].get_string() + R"(;')
    , '');
set @qry = if (@create_query != '', @create_query,
    'SET @r = \'Foreign key ")" + key["name"].get_string() + R"(" is ok.\';');
)";
                sql += exec;
            }
        }
    }

    // Apply users
    std::size_t index = 0;
    for (const auto& client : clients.get_array())
    {
        sql += R"(
set @old_user = null;
select `USER` into @old_user from `mysql`.`user`
where `USER` = ')" + client["user"].get_string() + R"(';
set @qry = if (isnull(@old_user),
    concat('CREATE USER \')" + client["user"].get_string() +
        R"(\' IDENTIFIED BY \'', MD5(RAND()), '\';')
,
    'SET @r = \'User ")" + client["user"].get_string() + R"(" exists.\';'
);
)";
        sql += exec;
        sql += R"(
set @all_grants = '';
)";
        // Revoke extra permissions
        for (const auto& permission : client["permissions"].get_array())
        {
            sql += R"(
set @all_grants = concat(@all_grants, ')" +
    permission["subject"].get_string() + R"( ');
)";
        }
        sql += R"(
set @sub_query = null;
select group_concat(concat('`', `table_name`, '`') separator ', ')
into @sub_query
from `mysql`.`tables_priv`
where
    `Db` = ')" + db_name + R"(' and
    `user` = ')" + client["user"].get_string() + R"(' and
    instr(@all_grants, `table_name`) = 0;
set @qry = if (isnull(@sub_query),
    'SET @r = \'No extra permissions for ")" + client["user"].get_string() +
        R"(".\';'
,
    'REVOKE ALL PRIVILEGES ON *.* FROM \')" + client["user"].get_string() +
        R"(\';'
);
)";
        sql += exec;

        // Grant permissions
        for (const auto& permission : client["permissions"].get_array())
        {
            std::string operations;
            for (const auto& operation : permission["operations"].get_array())
            {
                if (!operations.empty())
                {
                    operations += ",";
                }
                operations += operation.get_string();
            }
            sql += R"(
set @old_grant = null;
select `table_priv` into @old_grant
from `mysql`.`tables_priv`
where
    `Db` = ')" + db_name + R"(' and
    `user` = ')" + client["user"].get_string() + R"(' and
    `table_name` = ')" + permission["subject"].get_string() + R"(';
set @qry = if (@old_grant = ')" + operations + R"(',
    'SET @r = \'Permissions on ")" + permission["subject"].get_string() +
        R"(" for ")" + client["user"].get_string() + R"(" is ok.\';'
,
    'GRANT )" + operations + R"( ON `)" + db_name + R"(`.`)" +
    permission["subject"].get_string() + R"(` TO \')" +
    client["user"].get_string() + R"(\';'
);
)";
            sql += exec;
        }
    }

    return sql;
}
