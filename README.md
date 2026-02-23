# SQL Replication

"Sqlr" is a tool to maintain database schema.

The benefits of using "Sqlr":
- Uses the simplest form of the input without requiring SQL language
- Generates code without accessing the database server
- Doesn't require maintaining any kind of history

How the magic happens:
- Each table and column definition needs a GUID that shouldn't be changed through out the whole life of the project.

## Input

Developers define database schema in some simple Json files.

### Database

The database starts with a simple Json array file.

Example:
```
[
]
```

##### Table

A table is defined by a Json object in the root array of the database file. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| id | Yes | string | A GUID generated solely for this table |
| name | Yes | string | The name of the table |
| engine | No | string | The engine of the table |
| columns | Yes | array | The columns in the table |
| keys | No | array | The keys in the table |
| foreign-keys | No | array | The foreign-keys in the table |
| views | No | array | The views based on the table |
| rows | No | array | The rows to initialize the table |

Example:
```
{
    "name": "user",
    "id": "93B099B08D144B40BCC918FA24831669",
    "engine": "InnoDB",
    "columns": [
    ],
    "keys": [
    ],
    "foreign-keys": [
    ],
    "views": [
    ],
    "rows": [
    ]
}
```

##### Column

A column is defined by a Json object in the `columns` array of the table. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| id | Yes | string | A GUID generated solely for this column |
| name | Yes | string | The name of the column |
| type | Yes | string | The type of the column |
| auto | No | boolean | The column is auto generated or no |
| null | No | boolean | The column accepts null values or no |
| default | No | string | The default value for the column |

Example:
```
{
    "id": "76AC03C95026487AB55A590C48FE4C8F",
    "name": "id",
    "type": "int unsigned",
    "auto": true,
    "null": false,
    "default": ""
}
```

#### Key

A key is defined by a Json object in the `keys` array of the table. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| name | Yes | string | The name of the key |
| type | Yes | string | The type of the key |
| columns | Yes | array | The name of the columns of the key |

Example:
```
{
    "name": "PRIMARY",
    "type": "primary key",
    "columns": [
        "id"
    ]
}
```

#### Foreign Key

A foreign key is defined by a Json object in the `foreign-keys` array of the table. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| name | Yes | string | The name of the foreign key |
| delete | Yes | string | The delete option of the foreign key |
| update | Yes | string | The update option of the foreign key |
| columns | Yes | array | The name of the columns of the key |
| table | Yes | string | The name of the foreign table |
| keys | Yes | array | The name of columns in the foreign table |

Example:
```
{
    "name": "fk_member_user",
    "delete": "RESTRICT",
    "update": "RESTRICT",
    "columns": [
        "user"
    ],
    "table": "user",
    "keys": [
        "id"
    ]
}
```

#### View

A view is defined by a Json object in the `views` array of the table. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| name | Yes | string | The name of the view |
| columns | Yes | array | The name of the columns joining the view |
| joints | Yes | array | The joints in the view |

Example:
```
{
    "name": "membership",
    "columns": [
        "id"
    ],
    "joints": [
    ]
}
```

##### Joint

A joint is defined by a Json object in the `joints` array of the view. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| table | Yes | string | The name of the table joining the view |
| as | Yes | string | The alias for the table to be used in the view |
| type | Yes | string | The type of the joint |
| columns | Yes | array | The name of the columns of the table joining the view |
| ons | Yes | array | The relations between this table and the rest of the view |

Example:
```
{
    "table": "project",
    "as": "prj",
    "type": "inner",
    "columns": [
      "id"
    ],
    "ons": [
    ]
}
```

###### Relation

A relation is defined by a Json object in the `ons` array of the joint. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| foreign | Yes | string | The column used for comparison |
| base | Yes | object | The table and the column to compare with |

Example:
```
{
  "foreign": "project",
  "base": {
    "table": "project",
    "column": "id",
  }
}
```

#### Row

A row is defined by a Json object in the `rows` array of the table. Its has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| <column name> | Yes | string | The value for the column |
| ... | ... | ... | ... |

Example:
```
{
    "first_name": "John",
    "last_name": "Smith"
}
```

### Permissions

The permissions are optional and start with a simple Json array file.

Example:
```
[
]
```

#### User

A user is defined by a Json object in the root array of the permissions file. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| user | Yes | string | The username |
| permissions | Yes | array | The list of tables the user has access to |

Example:
```
{
  "user": "Bob",
  "permissions": [
  ]
}
```

#### Permission

A permission is defined by a Json object in the `permissions` array of the user object. It has the following fields:

| Field Name | Required | Type | Description |
| --- | --- | --- | --- |
| subject | Yes | string | The name of the table |
| operations | Yes | array | The list of operations the user can do on the table |

Example:
```
{
  "subject": "user",
  "operations": [
    "SELECT",
    "INSERT",
    "UPDATE",
    "DELETE"
  ]
}
```

### Remarks

The GUID of the tables and columns shouldn't be changed through out the lifetime of the project. Changing them will cause data loss.

## Output

The tool is called with the following parameters:
- database name
- database definition json
- permissions definition json
- report flag to add informative logs to SQL output
- dry-run flag to list required changes without applying them

The output is a SQL code that will apply required changes in a server.
