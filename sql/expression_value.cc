/** expression_value.h                                             -*- C++ -*-
    Jeremy Barnes, 14 February 2015
    Copyright (c) 2015 Datacratic Inc.  All rights reserved.

    This file is part of MLDB. Copyright 2015 Datacratic. All rights reserved.

    Code for the type that holds the value of an expression.
*/

#include "expression_value.h"
#include "sql_expression.h"
#include "mldb/types/structure_description.h"
#include "mldb/types/enum_description.h"
#include "mldb/types/vector_description.h"
#include "mldb/types/tuple_description.h"
#include "ml/value_descriptions.h"
#include "mldb/http/http_exception.h"
#include "mldb/jml/stats/distribution.h"
#include "mldb/utils/json_utils.h"
#include "mldb/types/hash_wrapper_description.h"
#include "mldb/jml/utils/less.h"
#include "mldb/jml/utils/lightweight_hash.h"

using namespace std;


namespace Datacratic {
namespace MLDB {

DEFINE_ENUM_DESCRIPTION(ColumnSparsity);

ColumnSparsityDescription::
ColumnSparsityDescription()
{
    addValue("dense", COLUMN_IS_DENSE, "Column is present in every row");
    addValue("sparse", COLUMN_IS_SPARSE, "Column is present in some rows");
}

DEFINE_ENUM_DESCRIPTION(StorageType);

StorageTypeDescription::
StorageTypeDescription()
{
    addValue("FLOAT32",    ST_FLOAT32,    "32 bit floating point number");
    addValue("FLOAT64",    ST_FLOAT64,    "64 bit floating point number");
    addValue("INT8",       ST_INT8,       "8 bit signed integer");
    addValue("UINT8",      ST_UINT8,      "8 bit unsigned integer");
    addValue("INT16",      ST_INT16,      "16 bit signed integer");
    addValue("UINT16",     ST_UINT16,     "16 bit unsigned integer");
    addValue("INT32",      ST_INT32,      "32 bit signed integer");
    addValue("UINT32",     ST_UINT32,     "32 bit unsigned integer");
    addValue("INT64",      ST_INT64,      "64 bit signed integer");
    addValue("UINT64",     ST_UINT64,     "64 bit unsigned integer");
    addValue("BLOB",       ST_BLOB,       "Binary blob");
    addValue("STRING",     ST_STRING,     "Ascii string");
    addValue("UTF8STRING", ST_UTF8STRING, "UTF-8 encoded string");
    addValue("ATOM",       ST_ATOM,       "Any atomic value");
    addValue("BOOL",       ST_BOOL,       "32 bit boolean value");
    addValue("TIMESTAMP",  ST_TIMESTAMP,  "Timestamp");
    addValue("INTERVAL",   ST_TIMEINTERVAL, "Time interval");
}

/*****************************************************************************/
/* EXPRESSION VALUE INFO                                                     */
/*****************************************************************************/

ExpressionValueInfo::
~ExpressionValueInfo()
{
}

std::shared_ptr<ExpressionValueInfo>
ExpressionValueInfo::
getCovering(const std::shared_ptr<ExpressionValueInfo> & info1,
            const std::shared_ptr<ExpressionValueInfo> & info2)
{
    // TODO: this is a hack to get going...

    if (&typeid(*info1) == &typeid(*info2))
        return info1;

    return std::make_shared<AnyValueInfo>();
}

std::shared_ptr<RowValueInfo>
ExpressionValueInfo::
getMerged(const std::shared_ptr<ExpressionValueInfo> & info1,
          const std::shared_ptr<ExpressionValueInfo> & info2,
          MergeColumnInfo mergeColumnInfo)
{
    auto cols1 = info1->getKnownColumns();
    auto cols2 = info2->getKnownColumns();

    std::unordered_map<ColumnName, std::pair<KnownColumn, KnownColumn> >
        allInfo;
    for (auto & c: cols1)
        allInfo[c.columnName].first = c;
    for (auto & c: cols2)
        allInfo[c.columnName].second = c;
    
    for (auto & c: allInfo) {
        if (c.second.first.valueInfo && c.second.second.valueInfo) {
            c.second.first.sparsity
                = c.second.first.sparsity == COLUMN_IS_SPARSE
                && c.second.second.sparsity == COLUMN_IS_SPARSE
                ? COLUMN_IS_SPARSE : COLUMN_IS_DENSE;
        }
        else if (c.second.first.valueInfo) {
            // Info is already there
        }
        else if (c.second.second.valueInfo) {
            c.second.first = std::move(c.second.second);
        }
        else {
            throw HttpReturnException(500, "Column with no value info");
        }

        if (mergeColumnInfo)
            c.second.first.valueInfo
                = mergeColumnInfo(c.first,
                                  c.second.first.valueInfo,
                                  c.second.second.valueInfo);
    }

    // Now, extract them in order of column name produced by the input
    std::vector<KnownColumn> colsOut;
    for (auto & c: cols1) {
        colsOut.emplace_back(std::move(allInfo[c.columnName].first));
        allInfo.erase(c.columnName);
    }

    // The second ones, which are assumed to be added once the first lot
    // have been dealt with
    for (auto & c: cols2) {
        if (!allInfo.count(c.columnName))
            continue;
        colsOut.emplace_back(std::move(allInfo[c.columnName].first));
    }

    SchemaCompleteness unk1 = info1->getSchemaCompleteness();
    SchemaCompleteness unk2 = info2->getSchemaCompleteness();

    return std::make_shared<RowValueInfo>(std::move(colsOut),
                                          (unk1 == SCHEMA_OPEN || unk2 == SCHEMA_OPEN
                                           ? SCHEMA_OPEN : SCHEMA_CLOSED));
}

std::shared_ptr<RowValueInfo>
ExpressionValueInfo::
getFlattenedInfo() const
{
    throw HttpReturnException(500, "Non-scalar values need to implement getFlattenedInfo()");
}

void
ExpressionValueInfo::
flatten(const ExpressionValue & value,
        const std::function<void (const ColumnName & columnName,
                                  const CellValue & value,
                                  Date timestamp)> & write) const
{
    throw HttpReturnException(500, "Non-scalar values need to implement flatten()");
}

bool
ExpressionValueInfo::
isRow() const
{
    return false;
}

bool
ExpressionValueInfo::
isEmbedding() const
{
    return false;
}

SchemaCompleteness
ExpressionValueInfo::
getSchemaCompleteness() const
{
    throw HttpReturnException(500, "Value description doesn't describe a row",
                              "type", ML::type_name(*this));
}

std::vector<KnownColumn>
ExpressionValueInfo::
getKnownColumns() const
{
    throw HttpReturnException(500, "Value description doesn't describe a row",
                              "type", ML::type_name(*this));
}

std::vector<ColumnName>
ExpressionValueInfo::
allColumnNames() const
{
    std::vector<ColumnName> result;
    for (auto & val: getKnownColumns())
        result.emplace_back(std::move(val.columnName));
    return result;
}

std::string
ExpressionValueInfo::
getScalarDescription() const
{
    throw HttpReturnException(500, "Value description doesn't describe a scalar",
                              "type", ML::type_name(*this));
}

struct ExpressionValueInfoPtrDescription
    : public ValueDescriptionT<std::shared_ptr<ExpressionValueInfo> > {

    virtual void parseJsonTyped(std::shared_ptr<ExpressionValueInfo> * val,
                                    JsonParsingContext & context) const
    {
        throw HttpReturnException(400, "ExpressionValueInfoPtrDescription::parseJsonTyped");
    }

    virtual void printJsonTyped(const std::shared_ptr<ExpressionValueInfo> * val,
                                JsonPrintingContext & context) const
    {
        Json::Value out;
        out["type"] = ML::type_name(**val);
        if ((*val)->isScalar()) {
            out["kind"] = "scalar";
            out["scalar"] = (*val)->getScalarDescription();
        }
        else if ((*val)->isEmbedding()) {
            out["kind"] = "embedding";
            out["shape"] = jsonEncode((*val)->getEmbeddingShape());
            out["type"] = jsonEncode((*val)->getEmbeddingType());
        }
        else if ((*val)->isRow()) {
            out["kind"] = "row";
            out["knownColumns"] = jsonEncode((*val)->getKnownColumns());
            out["hasUnknownColumns"] = (*val)->getSchemaCompleteness() == SCHEMA_OPEN;
        }
        context.writeJson(out);
    }

    virtual bool isDefaultTyped(const std::shared_ptr<ExpressionValueInfo> * val) const
    {
        return !*val;
    }
};

std::vector<ssize_t>
ExpressionValueInfo::
getEmbeddingShape() const
{
    return {};
}

StorageType
ExpressionValueInfo::
getEmbeddingType() const
{
    return ST_ATOM;
}

DEFINE_VALUE_DESCRIPTION_NS(std::shared_ptr<ExpressionValueInfo>,
                            ExpressionValueInfoPtrDescription);


/*****************************************************************************/
/* EXPRESSION VALUE INFO TEMPLATE                                            */
/*****************************************************************************/

template<typename Storage>
ExpressionValueInfoT<Storage>::
~ExpressionValueInfoT()
{
}

template<typename Storage>
size_t
ExpressionValueInfoT<Storage>::
getCellSize() const
{
    return sizeof(Storage);
}

template<typename Storage>
void
ExpressionValueInfoT<Storage>::
initCell(void * data) const
{
    new (data) Storage;
}

template<typename Storage>
void
ExpressionValueInfoT<Storage>::
initCopyFromCell(void * data, void * otherData) const
{
    new (data) Storage(*reinterpret_cast<Storage *>(otherData));
}

template<typename Storage>
void
ExpressionValueInfoT<Storage>::
initMoveFromCell(void * data, void * otherData) const
{
    new (data) Storage(std::move(*reinterpret_cast<Storage *>(otherData)));
}

template<typename Storage>
void
ExpressionValueInfoT<Storage>::
destroyCell(void * data) const
{
    auto val = reinterpret_cast<Storage *>(data);
    val->~Storage();
}

template<typename Storage>
void
ExpressionValueInfoT<Storage>::
moveFromCell(void * data, void * fromData) const
{
    auto val = reinterpret_cast<Storage *>(data);
    auto otherVal = reinterpret_cast<Storage *>(fromData);
    *val = std::move(*otherVal);
}

template<typename Storage>
void
ExpressionValueInfoT<Storage>::
copyFromCell(void * data, const void * fromData) const
{
    auto val = reinterpret_cast<Storage *>(data);
    auto otherVal = reinterpret_cast<const Storage *>(fromData);
    *val = *otherVal;
}


/*****************************************************************************/
/* EXPRESSION VALUE INSTANTIATIONS                                           */
/*****************************************************************************/

EmptyValueInfo::
~EmptyValueInfo()
{
}

size_t
EmptyValueInfo::
getCellSize() const
{
    return 0;
}

void
EmptyValueInfo::
initCell(void * data) const
{
}

void
EmptyValueInfo::
initCopyFromCell(void * data, void * otherData) const
{
}

void
EmptyValueInfo::
initMoveFromCell(void * data, void * otherData) const
{
}

void
EmptyValueInfo::
destroyCell(void * data) const
{
}

void
EmptyValueInfo::
moveFromCell(void * data, void * fromData) const
{
}

void
EmptyValueInfo::
copyFromCell(void * data, const void * fromData) const
{
}

bool
EmptyValueInfo::
isScalar() const
{
    return true;
}


/*****************************************************************************/
/* EMBEDDING VALUE INFO                                                      */
/*****************************************************************************/

EmbeddingValueInfo::
EmbeddingValueInfo(const std::vector<std::shared_ptr<ExpressionValueInfo> > & input)
{
    if (input.empty()) {
        return;
    }

    // First, check to see if our input info is a scalar or a vector
    if (input[0]->isScalar()) {
        
        std::shared_ptr<ExpressionValueInfo> info = input[0];
        for (auto & e: input) {
            if (!e->isScalar()) {
                throw HttpReturnException
                    (400, "Attempt to create scalar embedding element "
                     "from non-scalar",
                     "info", e);
            }
            info = ExpressionValueInfo::getCovering(info, e);
        }

        shape.push_back(input.size());
        return;
    }

    const EmbeddingValueInfo * emb
        = dynamic_cast<const EmbeddingValueInfo *>(input[0].get());

    if (emb) {
        // Recursive embedding
        shape.push_back(input.size());
        for (auto & d: emb->shape)
            shape.push_back(d);
        return;
    }

    // NOTE: this is required to make the usage of EmbeddingValueInfo in
    // JoinElement in the ExecutionPipeline work.  It's not really the
    // right tool for the job there and should be replaced with something
    // else.  At that point, we can make this back into an exception.
    shape = {-1};
    storageType = ST_ATOM;
    return;

    throw HttpReturnException(400, "Cannot determine embedding type from input types",
                              "inputTypes", input);
}

EmbeddingValueInfo::
EmbeddingValueInfo(std::vector<ssize_t> shape, StorageType storageType)
    : shape(std::move(shape)), storageType(storageType)
{
}

bool
EmbeddingValueInfo::
isScalar() const
{
    return false;
}

bool
EmbeddingValueInfo::
isRow() const
{
    return true;
}

bool
EmbeddingValueInfo::
isEmbedding() const
{
    return true;
}

bool
EmbeddingValueInfo::
couldBeScalar() const
{
    return shape.empty();
}

bool
EmbeddingValueInfo::
couldBeRow() const
{
    return true;
}

std::vector<ssize_t>
EmbeddingValueInfo::
getEmbeddingShape() const
{
    return shape;
}

StorageType
EmbeddingValueInfo::
getEmbeddingType() const
{
    return ST_ATOM;
}

size_t
EmbeddingValueInfo::
numDimensions() const
{
    return shape.size();
}

SchemaCompleteness
EmbeddingValueInfo::
getSchemaCompleteness() const
{
    // If we have a -1 index in the shape matrix, we don't know the size of
    // that dimension and thus which columns will be in it.
    for (auto & s: shape)
        if (s < 0)
            return SCHEMA_OPEN;
    return SCHEMA_CLOSED;
}


// TODO: generalize
std::shared_ptr<ExpressionValueInfo>
getValueInfoForStorage(StorageType type)
{
    switch (type) {
    case ST_FLOAT32:
        return std::make_shared<Float32ValueInfo>();
    case ST_FLOAT64:
        return std::make_shared<Float64ValueInfo>();
    case ST_INT8:
        return std::make_shared<IntegerValueInfo>();
    case ST_UINT8:
        return std::make_shared<IntegerValueInfo>();
        case ST_INT16:
            return std::make_shared<IntegerValueInfo>();
        case ST_UINT16:
            return std::make_shared<IntegerValueInfo>();
        case ST_INT32:
            return std::make_shared<IntegerValueInfo>();
        case ST_UINT32:
            return std::make_shared<IntegerValueInfo>();
        case ST_INT64:
            return std::make_shared<IntegerValueInfo>();
        case ST_UINT64:
            return std::make_shared<Uint64ValueInfo>();
        case ST_BLOB:
            return std::make_shared<BlobValueInfo>();
        case ST_STRING:
            return std::make_shared<StringValueInfo>();
        case ST_UTF8STRING:
            return std::make_shared<Utf8StringValueInfo>();
        case ST_ATOM:
            return std::make_shared<AtomValueInfo>();
        case ST_BOOL:
            return std::make_shared<BooleanValueInfo>();
        case ST_TIMESTAMP:
            return std::make_shared<TimestampValueInfo>();
        case ST_TIMEINTERVAL:
            return std::make_shared<AtomValueInfo>();
        }
                                            
        throw HttpReturnException(500, "Unknown embedding storage type",
                                  "type", type);
}

std::vector<KnownColumn>
EmbeddingValueInfo::
getKnownColumns() const
{
    std::vector<KnownColumn> result;
    
    // If we have a -1 index in the shape matrix, we don't know the size of
    // that dimension and thus how many values will be in it.
    for (auto & s: shape)
        if (s < 0)
            return result;

    auto valueInfo = getValueInfoForStorage(ST_ATOM);

    std::function<void (ColumnName prefix, int dim)> doDim
        = [&] (ColumnName prefix, int dim)
        {
            if (dim == shape.size()) {
                result.emplace_back(std::move(prefix), valueInfo, COLUMN_IS_DENSE);
                return;
            }
                                    
            for (size_t i = 0;  i < shape[dim];  ++i) {
                doDim(prefix + i, dim + 1);
            }
        };

    doDim(ColumnName(), 0);

    return result;
}

std::vector<ColumnName>
EmbeddingValueInfo::
allColumnNames() const
{
    std::vector<ColumnName> result;
    
    // If we have a -1 index in the shape matrix, we don't know the size of
    // that dimension and thus how many values will be in it.
    for (auto & s: shape)
        if (s < 0)
            return result;

    std::function<void (ColumnName prefix, int dim)> doDim
        = [&] (ColumnName prefix, int dim)
        {
            if (dim == shape.size()) {
                result.emplace_back(std::move(prefix));
                return;
            }
                                    
            for (size_t i = 0;  i < shape[dim];  ++i) {
                doDim(prefix + i, dim + 1);
            }
        };

    doDim(ColumnName(), 0);

    return result;
}

std::shared_ptr<RowValueInfo>
EmbeddingValueInfo::
getFlattenedInfo() const
{
    throw ML::Exception("EmbeddingValueInfo::getFlattenedInfo()");
}

void
EmbeddingValueInfo::
flatten(const ExpressionValue & value,
        const std::function<void (const ColumnName & columnName,
                                  const CellValue & value,
                                  Date timestamp)> & write) const
{
    throw ML::Exception("EmbeddingValueInfo::flatten()");
}

/*****************************************************************************/
/* ANY VALUE INFO                                                            */
/*****************************************************************************/

AnyValueInfo::
AnyValueInfo()
{
}

bool
AnyValueInfo::
isScalar() const
{
    return false;
}

std::shared_ptr<RowValueInfo>
AnyValueInfo::
getFlattenedInfo() const
{
    throw ML::Exception("AnyValueInfo::getFlattenedInfo()");
}

void
AnyValueInfo::
flatten(const ExpressionValue & value,
        const std::function<void (const ColumnName & columnName,
                                  const CellValue & value,
                                  Date timestamp)> & write) const
{
    throw ML::Exception("AnyValueInfo::flatten()");
}

SchemaCompleteness
AnyValueInfo::
getSchemaCompleteness() const
{
    return SCHEMA_OPEN;
}

std::vector<KnownColumn>
AnyValueInfo::
getKnownColumns() const
{
    return {};
}


/*****************************************************************************/
/* ROW VALUE INFO                                                            */
/*****************************************************************************/

RowValueInfo::
RowValueInfo(const std::vector<KnownColumn> & columns,
             SchemaCompleteness completeness)
    : columns(columns),
      completeness(completeness)
{
}

bool
RowValueInfo::
isScalar() const
{
    return false;
}

std::shared_ptr<RowValueInfo>
RowValueInfo::
getFlattenedInfo() const
{
    throw ML::Exception("RowValueInfo::getFlattenedInfo()");
}

void
RowValueInfo::
flatten(const ExpressionValue & value,
        const std::function<void (const ColumnName & columnName,
                                  const CellValue & value,
                                  Date timestamp)> & write) const
{
    throw ML::Exception("RowValueInfo::flatten()");
}

std::vector<KnownColumn>
RowValueInfo::
getKnownColumns() const
{
    return columns;
}

SchemaCompleteness
RowValueInfo::
getSchemaCompleteness() const
{
    return completeness;
}

std::shared_ptr<ExpressionValueInfo> 
RowValueInfo::
findNestedColumn(const Utf8String& variableName, SchemaCompleteness& schemaCompleteness)
{  
    for (auto& col : columns)
    {
         if (col.columnName == variableName)
         {
             return col.valueInfo;
         }
    }   

    auto it = variableName.find('.');
    if (it != variableName.end())
    {
        Utf8String head(variableName.begin(),it);
        ++it;

        for (auto& col : columns)
        {
             if (col.columnName == head)
             {
                 Utf8String tail(it, variableName.end());
                 return col.valueInfo->findNestedColumn(tail, schemaCompleteness);
             }
         }   
    }

    schemaCompleteness = getSchemaCompleteness();
    return std::shared_ptr<ExpressionValueInfo>();
}            


/*****************************************************************************/
/* EXPRESSION VALUE                                                          */
/*****************************************************************************/

/// This is how we store a structure with a single value for each
/// element and an external set of column names.  There is only
/// one timestamp for the whole thing.
struct ExpressionValue::Struct {
    std::shared_ptr<const std::vector<ColumnName> > columnNames;
    std::vector<CellValue> values;

    size_t length() const
    {
        return values.size();
    }

    CellValue value(int i) const
    {
        return values.at(i);
    }

    CellValue moveValue(int i)
    {
        return std::move(values.at(i));
    }

    ColumnName columnName(int i) const
    {
        if (columnNames)
            return columnNames->at(i);
        // TODO: static list of the first 1,000 column names to avoid allocs
        else return ColumnName(i);
    }
};

/// This is how we store a embedding, which is a dense array of a
/// uniform data type.
struct ExpressionValue::Embedding {
    std::shared_ptr<const void> data_;
    StorageType storageType_;
    std::vector<size_t> dims_;
    std::shared_ptr<const EmbeddingMetadata> metadata_;

    size_t length() const
    {
        size_t result = 1;
        for (auto & d: dims_)
            result *= d;
        return result;
    }

    template<typename T>
    const T & getValueT(size_t n) const
    {
        return n[(T *)(data_.get())];
    }

    CellValue getValue(size_t n) const
    {
        switch (storageType_) {
        case ST_FLOAT32:
            return getValueT<float>(n);
        case ST_FLOAT64:
            return getValueT<double>(n);
        case ST_INT8:
            return getValueT<int8_t>(n);
        case ST_UINT8:
            return getValueT<uint8_t>(n);
        case ST_INT16:
            return getValueT<int16_t>(n);
        case ST_UINT16:
            return getValueT<uint16_t>(n);
        case ST_INT32:
            return getValueT<int32_t>(n);
        case ST_UINT32:
            return getValueT<uint32_t>(n);
        case ST_INT64:
            return getValueT<int64_t>(n);
        case ST_UINT64:
            return getValueT<uint64_t>(n);
        case ST_BLOB:
            return CellValue::blob(getValueT<std::string>(n));
        case ST_STRING:
            return getValueT<std::string>(n);
        case ST_UTF8STRING:
            return getValueT<Utf8String>(n);
        case ST_ATOM:
            return getValueT<CellValue>(n);
        case ST_BOOL:
            return getValueT<bool>(n);
        case ST_TIMESTAMP:
            return getValueT<Date>(n);
        case ST_TIMEINTERVAL:
            throw HttpReturnException(500, "Can't store time intervals");
        }
        
        throw HttpReturnException(500, "Unknown embedding storage type",
                                  "storageType", storageType_);
    }

    bool forEachValue(std::function<bool (const std::vector<int> & indexes,
                                          CellValue & val)> onVal) const
    {
        vector<int> indexes(dims_.size());
        size_t n = 0;

        std::function<bool (int level)> runLevel = [&] (int i)
            {
                if (i == dims_.size()) {
                    CellValue val = getValue(n++);
                    return onVal(indexes, val);
                }
                else {
                    for (indexes[i] = 0;  indexes[i] < dims_[i];
                         ++indexes[i]) {
                        if (!runLevel(i + 1))
                            return false;
                    }
                    return true;
                }
            };

        return runLevel(0);
    }

    bool forEachColumn(std::function<bool (ColumnName & col,
                                           CellValue & val)> onColumn) const
    {
        auto onValue = [&] (const vector<int> & indexes, CellValue & val)
            {
                Coord c;
                for (auto & i: indexes) {
                    c = c + i;
                }

                return onColumn(c, val);
            };

        return forEachValue(onValue);
    }

    void writeJson(JsonPrintingContext & context) const
    {
        context.startObject();
        context.startMember("shape");
        context.writeJson(jsonEncode(dims_));
        context.startMember("type");
        context.writeString(jsonEncode(storageType_).asString());
        context.startMember("val");

        vector<int> indexes(dims_.size());
        size_t n = 0;

        std::function<void (int level)> runLevel = [&] (int i)
            {
                static auto cellDesc = getDefaultDescriptionShared((CellValue *)0);
                if (i == dims_.size()) {
                    CellValue val(getValue(n++));
                    cellDesc->printJsonTyped(&val, context);
                }
                else {
                    context.startArray(dims_[i]);
                    for (indexes[i] = 0;  indexes[i] < dims_[i];
                         ++indexes[i]) {
                        context.newArrayElement();
                        runLevel(i + 1);
                    }
                    context.endArray();
                }
            };
        
        runLevel(0);

        context.endObject();
    }
};


static_assert(sizeof(CellValue) <= 24, "CellValue is too big to fit");
static_assert(sizeof(ExpressionValue::Row) <= 24, "Row is too big to fit");

ExpressionValue::
ExpressionValue()
    : type_(NONE)
{
}

ExpressionValue::
ExpressionValue(std::nullptr_t, Date ts)
    : type_(NONE), ts_(ts)
{
}

ExpressionValue
ExpressionValue::
null(Date ts)
{
    return ExpressionValue(nullptr, ts);
}

ExpressionValue::
ExpressionValue(Date val, Date ts)
    : type_(NONE)
{
    initAtom(val, ts);
}

ExpressionValue::
ExpressionValue(const std::string & asciiStringValue, Date ts)
    : type_(NONE)
{
    initAtom(asciiStringValue, ts);
}

ExpressionValue::
ExpressionValue(const char * asciiStringValue, Date ts)
    : type_(NONE)
{
    initAtom(asciiStringValue, ts);
}

ExpressionValue::
ExpressionValue(const Utf8String & unicodeStringValue, Date ts)
    : type_(NONE)
{
    initAtom(unicodeStringValue, ts);
}

ExpressionValue::
ExpressionValue(const std::basic_string<char32_t> & utf32StringValue, Date ts)
    : type_(NONE)
{
    initAtom(Utf8String(utf32StringValue), ts);
}

#if 0
ExpressionValue::
ExpressionValue(const char16_t * unicodeStringValue, Date ts)
    : type_(NONE)
{
    initAtom(unicodeStringValue, ts);
}

ExpressionValue::
ExpressionValue(const char32_t * unicodeStringValue, Date ts)
    : type_(NONE)
{
    initAtom(unicodeStringValue, ts);
}
#endif

ExpressionValue::
ExpressionValue(CellValue atom, Date ts) noexcept
    : type_(NONE)
{
    initAtom(std::move(atom), ts);
}

ExpressionValue::
ExpressionValue(const Json::Value & val, Date timestamp)
    : type_(NONE)
{
    if (!val.isObject() && !val.isArray()) {
        initAtom(jsonDecode<CellValue>(val), timestamp);
        return;
    }

    ts_ = timestamp;

    StructValue row;

    if (val.isObject()) {
        for (auto it = val.begin(), end = val.end();  it != end;  ++it) {
            ColumnName columnName(it.memberName());
            row.emplace_back(columnName, ExpressionValue(*it, timestamp));
        }
    }
    else if (val.isArray()) {
        for (unsigned i = 0;  i < val.size();  ++i) {
            ColumnName columnName(i);
            row.emplace_back(columnName, ExpressionValue(val[i], timestamp));
        }
    }

    initRow(std::move(row));
}

static const auto cellValueDesc = getDefaultDescriptionShared((CellValue *)0);

CellValue expectAtom(JsonParsingContext & context)
{
    CellValue val;
    cellValueDesc->parseJson(&val, context);
    return val;
}

ExpressionValue
ExpressionValue::
parseJson(JsonParsingContext & context,
          Date timestamp,
          JsonArrayHandling arrays)
{
    if (context.isObject()) {

        std::vector<std::tuple<ColumnName, ExpressionValue> > out;

        auto onObjectField = [&] ()
            {
                const char * fieldName = context.fieldNamePtr();
                out.emplace_back
                (ColumnName(fieldName, strlen(fieldName)),
                 parseJson(context, timestamp, arrays));
            };
        context.forEachMember(onObjectField);

        return std::move(out);
    }
    else if (context.isArray()) {
        std::vector<std::tuple<ColumnName, ExpressionValue> > out;

        bool hasNonAtom = false;
        bool hasNonObject = false;

        auto onArrayElement = [&] ()
            {
                if (!context.isObject())
                    hasNonObject = true;
                out.emplace_back(ColumnName(context.fieldNumber()),
                                 parseJson(context, timestamp, arrays));
                
                if (!std::get<1>(out.back()).isAtom())
                    hasNonAtom = true;
            };
        
        context.forEachElement(onArrayElement);

        if (arrays == ENCODE_ARRAYS && !hasNonAtom) {
            // One-hot encode them
            for (auto & v: out) {
                ColumnName & columnName = std::get<0>(v);
                ExpressionValue & columnValue = std::get<1>(v);
                
                columnName = ColumnName(columnValue.toUtf8String());
                columnValue = ExpressionValue(1, timestamp);
            }
        }
        else if (arrays == ENCODE_ARRAYS && !hasNonObject) {
            // JSON encode them
            for (auto & v: out) {
                ExpressionValue & columnValue = std::get<1>(v);
                std::string str;
                StringJsonPrintingContext context(str);
                columnValue.extractJson(context);
                columnValue = ExpressionValue(str, timestamp);
            }
        }

        return std::move(out);
    }
    else {
        // It's an atomic cell value; parse the atom
        return ExpressionValue(expectAtom(context), timestamp);
    }
}

ExpressionValue::
ExpressionValue(RowValue row) noexcept
: type_(NONE)
{
    Row row2;
    row2.reserve(row.size());

    for (auto & c: row) {
        row2.emplace_back(std::move(std::get<0>(c)),
                          ExpressionValue(std::move(std::get<1>(c)), std::get<2>(c)));
    }
    
    initRow(std::move(row2));
}

#if 0
ExpressionValue::
ExpressionValue(const ML::distribution<float> & embedding, Date ts)
    : type_(NONE)
{
    initEmbedding(embedding, ts);
}

ExpressionValue::
ExpressionValue(const ML::distribution<double> & embedding, Date ts)
    : type_(NONE)
{
    initEmbedding(embedding, ts);
}
#endif

ExpressionValue::
~ExpressionValue()
{
    typedef std::shared_ptr<const Row> RowRepr;
    typedef std::shared_ptr<const Struct> StructRepr;
    typedef std::shared_ptr<const Embedding> EmbeddingRepr;

    switch (type_) {
    case NONE: return;
    case ATOM: cell_.~CellValue();  return;
    case ROW:  row_.~RowRepr();  return;
    case STRUCT: struct_.~StructRepr();  return;
    case EMBEDDING: embedding_.~EmbeddingRepr();  return;
    }
    throw HttpReturnException(400, "Unknown expression value type");
}

ExpressionValue::
ExpressionValue(const ExpressionValue & other)
    : type_(NONE)
{
    switch (other.type_) {
    case NONE: ts_ = other.ts_;  return;
    case ATOM: initAtom(other.cell_, other.ts_);  return;
    case ROW:  initRow(other.row_);  return;
    case STRUCT: {
        ts_ = other.ts_;
        new (storage_) std::shared_ptr<const Struct>(other.struct_);
        type_ = STRUCT;
        return;
    }
    case EMBEDDING: {
        ts_ = other.ts_;
        new (storage_) std::shared_ptr<const Embedding>(other.embedding_);
        type_ = EMBEDDING;
        return;
    }
    }
    throw HttpReturnException(400, "Unknown expression value type");
}

ExpressionValue::
ExpressionValue(ExpressionValue && other) noexcept
    : type_(NONE)
{
    ts_ = other.ts_;
    type_ = other.type_;

    switch (other.type_) {
    case NONE: return;
    case ATOM: new (storage_) CellValue(std::move(other.cell_));  return;
    case ROW:  new (storage_) std::shared_ptr<const Row>(std::move(other.row_));  return;
    case STRUCT: new (storage_) std::shared_ptr<const Struct>(std::move(other.struct_));  return;
    case EMBEDDING: new (storage_) std::shared_ptr<const Embedding>(std::move(other.embedding_));  return;
    }
    throw HttpReturnException(400, "Unknown expression value type");
}

ExpressionValue::
ExpressionValue(std::vector<std::tuple<Coord, ExpressionValue> > vals) noexcept
    : type_(NONE)
{
    initRow(std::move(vals));
}

ExpressionValue &
ExpressionValue::
operator = (const ExpressionValue & other)
{
    ExpressionValue newMe(other);
    swap(newMe);
    return *this;
}

ExpressionValue &
ExpressionValue::
operator = (ExpressionValue && other) noexcept
{
    ExpressionValue newMe(std::move(other));
    swap(newMe);
    return *this;
}

void
ExpressionValue::
swap(ExpressionValue & other) noexcept
{
    // Dodgy as hell.  But none of them have self referential pointers, and so it
    // works and with no possibility of an exception.
    std::swap(type_,    other.type_);
    std::swap(storage_[0], other.storage_[0]);
    std::swap(storage_[1], other.storage_[1]);
    std::swap(ts_, other.ts_);
}

ExpressionValue::
ExpressionValue(std::vector<CellValue> values,
                std::shared_ptr<const std::vector<ColumnName> > cols,
                Date ts)
    : type_(NONE), ts_(ts)
{
    ExcAssertEqual(values.size(), (*cols).size());

    std::shared_ptr<Struct> content(new Struct());
    content->values = std::move(values);
    content->columnNames = std::move(cols);

    new (storage_) std::shared_ptr<const Struct>(std::move(content));
    type_ = STRUCT;
}

ExpressionValue::
ExpressionValue(std::vector<CellValue> values,
                Date ts,
                std::vector<size_t> shape)
    : type_(NONE), ts_(ts)
{
    std::shared_ptr<CellValue> vals(new CellValue[values.size()],
                                    [] (CellValue * p) { delete[] p; });
    std::copy(std::make_move_iterator(values.begin()),
              std::make_move_iterator(values.end()),
              vals.get());
    std::shared_ptr<Embedding> content(new Embedding());
    content->data_ = std::move(vals);
    content->storageType_ = ST_ATOM;
    content->dims_ = std::move(shape);
    if (content->dims_.empty())
        content->dims_ = { values.size() };
    
    new (storage_) std::shared_ptr<const Embedding>(std::move(content));
    type_ = EMBEDDING;
}

ExpressionValue::
ExpressionValue(const std::vector<float> & values,
                std::shared_ptr<const std::vector<ColumnName> > cols,
                Date ts)
    : type_(NONE), ts_(ts)
{
    ExcAssertEqual(values.size(), (*cols).size());

    std::shared_ptr<Struct> content(new Struct());
    vector<CellValue> cellValues(values.begin(), values.end());
    content->values = std::move(cellValues);
    content->columnNames = std::move(cols);

    new (storage_) std::shared_ptr<const Struct>(std::move(content));
    type_ = STRUCT;
}

ExpressionValue::
ExpressionValue(std::vector<float> values, Date ts,
                std::vector<size_t> shape)
    : type_(NONE), ts_(ts)
{
    std::shared_ptr<float> vals(new float[values.size()],
                                    [] (float * p) { delete[] p; });
    std::copy(std::make_move_iterator(values.begin()),
              std::make_move_iterator(values.end()),
              vals.get());
    std::shared_ptr<Embedding> content(new Embedding());
    content->data_ = std::move(vals);
    content->storageType_ = ST_FLOAT32;
    content->dims_ = std::move(shape);
    if (content->dims_.empty())
        content->dims_ = { values.size() };
    
    new (storage_) std::shared_ptr<const Embedding>(std::move(content));
    type_ = EMBEDDING;
}

ExpressionValue::
ExpressionValue(std::vector<double> values, Date ts,
                std::vector<size_t> shape)
    : type_(NONE), ts_(ts)
{
    std::shared_ptr<double> vals(new double[values.size()],
                                    [] (double * p) { delete[] p; });
    std::copy(std::make_move_iterator(values.begin()),
              std::make_move_iterator(values.end()),
              vals.get());
    std::shared_ptr<Embedding> content(new Embedding());
    content->data_ = std::move(vals);
    content->storageType_ = ST_FLOAT64;
    content->dims_ = std::move(shape);
    if (content->dims_.empty())
        content->dims_ = { values.size() };
    
    new (storage_) std::shared_ptr<const Embedding>(std::move(content));
    type_ = EMBEDDING;
}

ExpressionValue
ExpressionValue::
embedding(Date ts,
       std::shared_ptr<const void> data,
       StorageType storageType,
       std::vector<size_t> dims,
       std::shared_ptr<const EmbeddingMetadata> md)
{
    auto embeddingData = std::make_shared<Embedding>();
    embeddingData->data_ = std::move(data);
    embeddingData->storageType_ = storageType;
    embeddingData->dims_ = std::move(dims);
    embeddingData->metadata_ = std::move(md);

    ExpressionValue result;
    result.ts_ = ts;
    result.type_ = EMBEDDING;
    new (result.storage_) std::shared_ptr<const Embedding>(std::move(embeddingData));

    return result;
}

ExpressionValue
ExpressionValue::
fromInterval(uint16_t months, uint16_t days, float seconds, Date ts)
{
    CellValue cellValue = CellValue::fromMonthDaySecond(months, days ,seconds);
    return ExpressionValue(cellValue, ts);
}

double
ExpressionValue::
toDouble() const
{
    //ExcAssertEqual(type_, ATOM);
   if (type_ != ATOM)
        throw HttpReturnException(400, "Can't convert from " + getTypeAsUtf8String() + " to double");
    return cell_.toDouble();
}

int64_t
ExpressionValue::
toInt() const
{
    if (type_ != ATOM)
        throw HttpReturnException(400, "Can't convert from " + getTypeAsUtf8String() + " to integer");
    return cell_.toInt();
}
    
bool
ExpressionValue::
asBool() const
{
    if (type_ == NONE)
        return false;
    
    if (type_ != ATOM)
        throw HttpReturnException(400, "Can't convert from " + getTypeAsUtf8String() + " to boolean");

    return cell_.asBool();
}

bool
ExpressionValue::
isTrue() const
{
    switch (type_) {
    case NONE:
        return false;
    case ATOM:
        return cell_.isTrue();
    case ROW:
        return !row_->empty();
    case STRUCT:
        return struct_->length();
    case EMBEDDING:
        return embedding_->length();
    }

    throw HttpReturnException(500, "Unknown expression value type");
}

bool
ExpressionValue::
isFalse() const
{
    switch (type_) {
    case NONE:
        return false;
    case ATOM:
        return cell_.isFalse();
    case ROW:
        return row_->empty();
    case STRUCT:
        return !struct_->length();
    case EMBEDDING:
        return embedding_->length();
    }

    throw HttpReturnException(500, "Unknown expression value type");
}

bool
ExpressionValue::
empty() const
{
    if (type_ == NONE)
        return true;
    if (type_ == ATOM)
        return cell_.empty();
    return false;
}

bool
ExpressionValue::
isString() const
{
    if (type_ != ATOM)
        return false;
    return cell_.isString();
}

bool
ExpressionValue::
isUtf8String() const
{
    if (type_ != ATOM)
        return false;
    return cell_.isUtf8String();
}

bool
ExpressionValue::
isAsciiString() const
{
    if (type_ != ATOM)
        return false;
    return cell_.isAsciiString();
}

bool
ExpressionValue::
isInteger() const
{
    if (type_ != ATOM)
        return false;
    return cell_.isInteger();
}

bool
ExpressionValue::
isNumber() const
{
    if (type_ != ATOM)
        return false;
    return cell_.isNumber();
}

bool
ExpressionValue::
isTimestamp() const
{
    if (type_ != ATOM)
        return false;
    return cell_.isTimestamp();
}

bool
ExpressionValue::
isTimeinterval() const
{
    if (type_ != ATOM)
        return false;
    return getAtom().isTimeinterval();
}

bool
ExpressionValue::
isObject() const
{
    return type_ == ROW;
}

bool
ExpressionValue::
isArray() const
{
    return type_ == ROW;
}

bool
ExpressionValue::
isAtom() const
{
    return type_ == ATOM || type_ == NONE;
}

bool
ExpressionValue::
isRow() const
{
    return type_ == ROW || type_ == STRUCT;
}

bool
ExpressionValue::
isEmbedding() const
{
    return type_ == EMBEDDING;
}

std::string
ExpressionValue::
toString() const
{
    if (type_ != ATOM)
        throw HttpReturnException(400, "Can't convert from " + getTypeAsUtf8String() + " to string");
    return cell_.toString();
}

Utf8String
ExpressionValue::
toUtf8String() const
{
    if (type_ != ATOM)
        throw HttpReturnException(400, "Can't convert from " + getTypeAsUtf8String() + " to UTF8 string");
    return cell_.toUtf8String();
}

Utf8String
ExpressionValue::
getTypeAsUtf8String() const
{
    switch (type_)
    {
        case NONE:
            return "empty";
        case ATOM:
            return "atomic value";
        case ROW:
            return "row";
        case STRUCT:
            return "embedding";
        default:
            ExcAssert(false);
            return "unknown";
    }
}

std::basic_string<char32_t>
ExpressionValue::
toWideString() const
{
    if (type_ != ATOM)
        throw HttpReturnException(400, "Can't convert from " + getTypeAsUtf8String() + " to wide string");
    return cell_.toWideString();
}

Date
ExpressionValue::
getMinTimestamp() const
{
    if (type_ == NONE || type_ == ATOM)
        return ts_;

    Date result = Date::positiveInfinity();

    auto onSubex = [&] (const Coord & columnName,
                        const Coord & prefix,
                        const ExpressionValue & val)
        {
            result.setMin(val.getMinTimestamp());
            return true;
        };

    forEachSubexpression(onSubex);

    return result;
}

Date
ExpressionValue::
getMaxTimestamp() const
{
    if (type_ == NONE || type_ == ATOM)
        return ts_;

    Date result = Date::negativeInfinity();

    auto onSubex = [&] (const Coord & columnName,
                        const Coord & prefix,
                        const ExpressionValue & val)
        {
            result.setMax(val.getMaxTimestamp());
            return true;
        };
    
    forEachSubexpression(onSubex);

    return result;
}

bool 
ExpressionValue::
isEarlier(const Date& compareTimeStamp, const ExpressionValue& compareValue) const
{
    Date ts = getEffectiveTimestamp();
    return (ts < compareTimeStamp) || (ts == compareTimeStamp && *this < compareValue);
}

bool 
ExpressionValue::
isLater(const Date& compareTimeStamp, const ExpressionValue& compareValue) const
{
    Date ts = getEffectiveTimestamp();
    return (ts > compareTimeStamp) || (ts == compareTimeStamp && *this < compareValue);
}

ExpressionValue
ExpressionValue::
getField(const Utf8String & fieldName, const VariableFilter & filter) const
{
    switch (type_) {
    case ROW: {
        ColumnName colName(fieldName);

        ExpressionValue storage;
        const ExpressionValue * result = searchRow(*row_, colName, filter, storage);
        if (result) {
            if (result == &storage)
                return std::move(storage);
            else return *result;
        }

        return ExpressionValue();
    }
    case STRUCT: {
        // TODO: any / latest / etc...
        ColumnName colName(fieldName);

        for (unsigned i = 0;  i != struct_->length();  ++i) {
            if (colName == struct_->columnName(i))
                return ExpressionValue(struct_->value(i), ts_);
        }
        return ExpressionValue();
    }
    case EMBEDDING: {
        throw ML::Exception("TODO: field access for embedding type");
    }
    case NONE:
    case ATOM:
        break;
    }
    return ExpressionValue();
}

ExpressionValue
ExpressionValue::
getField(int fieldIndex) const
{
    if (type_ == STRUCT) {
        return ExpressionValue(struct_->value(fieldIndex), ts_);
    }
    else if (type_ == EMBEDDING) {
        return ExpressionValue(embedding_->getValue(fieldIndex), ts_);
    }

    return ExpressionValue();
}

const ExpressionValue*
ExpressionValue::
findNestedField(const Utf8String & fieldName, const VariableFilter & filter /*= GET_LATEST*/) const
{
    if (type_ == ROW) {

        ColumnName colName(fieldName);

        ExpressionValue storage;
        const ExpressionValue * result = searchRow(*row_, colName, filter, storage);
        if (result) {
            if (result == &storage)
                return nullptr; // we want an address, no good
            else 
                return result;
        }
        else
        {
            auto it = fieldName.find('.');
            if (it != fieldName.end())
            {
                Utf8String head(fieldName.begin(),it);
                ++it;

                ColumnName colName(head);

                result = searchRow(*row_, colName, filter, storage);
                if (result && result != &storage) 
                {
                    Utf8String tail(it, fieldName.end());
                    return result->findNestedField(tail, filter);
                }   
            }
        }
    }
    else if (type_ == STRUCT) {
        throw HttpReturnException(400, "findNestedField for struct");
        
    }
    else if (type_ == EMBEDDING) {
        throw HttpReturnException(400, "findNestedField for embedding");
    }

    return nullptr;
}

std::vector<size_t>
ExpressionValue::
getEmbeddingShape() const
{
    switch (type_) {
    case NONE:
    case ATOM:
        return {};
    case STRUCT:
        return { struct_->length() };
    case ROW:
        return { row_->size() };
    case EMBEDDING:
        return embedding_->dims_;
    }

    throw HttpReturnException(500, "Unknown storage type for getEmbeddingShape()");
}

ML::distribution<float, std::vector<float> >
ExpressionValue::
getEmbedding(ssize_t knownLength) const
{
    return getEmbeddingDouble(knownLength).cast<float>();
}

ML::distribution<double, std::vector<double> >
ExpressionValue::
getEmbeddingDouble(ssize_t knownLength) const
{
    //cerr << "getEmbedding for " << jsonEncode(*this) << endl;

    // TODO: this is inefficient.  We should be able to have the
    // info function return us one that does it much more
    // efficiently.

    std::vector<std::pair<ColumnName, double> > features;
    if (knownLength != -1)
        features.reserve(knownLength);

    auto onAtom = [&] (const ColumnName & columnName,
                       const ColumnName & prefix,
                       const CellValue & val,
                       Date ts)
        {
            features.emplace_back(columnName, val.toDouble());
            return true;
        };
    
    forEachAtom(onAtom);

    std::sort(features.begin(), features.end());

    ML::distribution<double> result;
    result.reserve(features.size());
    for (unsigned i = 0;  i < features.size();  ++i) {
        result.push_back(features[i].second);
        if (result.size() == knownLength)
            break;
    }
    
    if (knownLength != -1 && result.size() != knownLength)
        throw HttpReturnException(400, Utf8String("Expected ") + to_string(knownLength) + " elements in embedding, got " + to_string(result.size()));;

    //cerr << "embedding result is " << result << endl;

    return result;
}

ML::distribution<float, std::vector<float> >
ExpressionValue::
getEmbedding(const std::vector<ColumnName> & knownNames,
             ssize_t maxLength,
             size_t numDone) const
{
    //cerr << "getEmbedding for " << jsonEncode(*this) << endl;

    //cerr << "in getEmbedding with type " << type_ << endl;

    ML::distribution<float> features;
    if (maxLength != -1)
        features.reserve(maxLength);

    if (type_ == STRUCT) {
        if (!struct_->values.empty()) {
            for (unsigned i = 0;  i < struct_->values.size();  ++i) {
                if (maxLength != -1 && features.size() >= maxLength)
                    break;
                features.push_back(struct_->values[i].toDouble());
                // TODO: assert on name
            }
        } else {
            if (maxLength == -1)
                maxLength = struct_->length();
            features.reserve(struct_->values.size());
            for (auto & v: struct_->values)
                features.emplace_back(v.toDouble());
        }
        return features;
    }
    else if (type_ == EMBEDDING) {
        throw HttpReturnException(400, "getEmbedding for embedding");
    }
    
    auto onSubexpression = [&] (const ColumnName & columnName,
                                const ColumnName & prefix,
                                const ExpressionValue & val)
        {
            ssize_t numToAdd = -1;
            if (maxLength != -1) {
                numToAdd = maxLength - features.size();
                if (numToAdd <= 0)
                    return false;
            }

            ML::distribution<float> subEmbedding
                = val.getEmbedding(knownNames, numToAdd, features.size());

            if (numToAdd == -1) {
                features.insert(features.end(),
                                subEmbedding.begin(),
                                subEmbedding.end());
            }
            else {
                if (subEmbedding.size() < numToAdd)
                    numToAdd = subEmbedding.size();
                
                features.insert(features.end(),
                                subEmbedding.begin(),
                                subEmbedding.begin() + numToAdd);
            }

            //cerr << "sub getEmbedding " << columnName << " " << prefix
            //<< " " << val.type_ << " " << jsonEncodeStr(val) << endl;
            return maxLength == -1 || features.size() < maxLength;
            
        };

    forEachSubexpression(onSubexpression);

    return features;
}

std::vector<CellValue>
ExpressionValue::
getEmbeddingCell(ssize_t knownLength) const
{
    std::vector<CellValue> result;
    if (knownLength > 0)
        result.reserve(knownLength);

    if (type_ == EMBEDDING) {
        ExcAssert(embedding_);
        size_t len = embedding_->length();
        for (size_t i = 0;  i < len;  ++i)
            result.emplace_back(embedding_->getValue(i));
        return result;
    }

    throw HttpReturnException(500, "getEmbeddingCell called for non-embedding");
}

void
ExpressionValue::
appendToRow(const ColumnName & columnName, MatrixNamedRow & row) const
{
    appendToRow(columnName, row.columns);
}

void
ExpressionValue::
appendToRow(const ColumnName & columnName, RowValue & row) const
{
    auto onAtom = [&] (const ColumnName & columnName,
                       const Coord & prefix,
                       const CellValue & val,
                       Date ts)
        {
            if (prefix == Coord()) {
                row.emplace_back(columnName, val, ts);
            }
            else if (columnName == ColumnName()) {
                row.emplace_back(prefix, val, ts);
            }
            else {
                row.emplace_back(prefix + columnName, val, ts);
            }
            return true;
        };

    forEachAtom(onAtom, columnName);
}

void
ExpressionValue::
appendToRow(const Coord & columnName, StructValue & row) const
{
    auto onSubexpr = [&] (const ColumnName & columnName,
                          const Coord & prefix,
                          const ExpressionValue & val)
        {
            row.emplace_back(prefix + columnName, val);
            return true;
        };

    forEachSubexpression(onSubexpr, columnName);
}

void
ExpressionValue::
appendToRowDestructive(ColumnName & columnName, RowValue & row)
{
    if (type_ == NONE) {
        row.emplace_back(std::move(columnName), CellValue(), ts_);
    }
    else if (type_ == ATOM) {
        row.emplace_back(std::move(columnName), stealAtom(), ts_);
    }
    else {
        if (row.capacity() == 0)
            row.reserve(rowLength());
        else if (row.capacity() < row.size() + rowLength())
            row.reserve(row.capacity() * 2);

        auto onSubexpr = [&] (ColumnName & innerColumnName,
                              ExpressionValue & val)
            {
                ColumnName newColumnName;
                if (columnName.empty())
                    newColumnName = std::move(innerColumnName);
                else if (innerColumnName.empty())
                    newColumnName = columnName;
                else newColumnName = ColumnName(columnName.toUtf8String()
                                                + "."
                                                + innerColumnName.toUtf8String());
                val.appendToRowDestructive(newColumnName, row);
                return true;
            };

        forEachColumnDestructiveT(onSubexpr);
    }
}


size_t
ExpressionValue::
rowLength() const
{
    if (type_ == ROW) {
        return row_->size();
    }
    else if (type_ == STRUCT) {
        return struct_->length();
    }
    else if (type_ == EMBEDDING) {
        return embedding_->length();
    }
    else throw HttpReturnException(500, "Attempt to access non-row as row",
                                   "value", *this);
}

void
ExpressionValue::
mergeToRowDestructive(StructValue & row)
{
    row.reserve(row.size() + rowLength());

    auto onSubexpr = [&] (ColumnName & columnName,
                          ExpressionValue & val)
        {
            row.emplace_back(std::move(columnName), std::move(val));
            return true;
        };

    forEachColumnDestructiveT(onSubexpr);
}

void 
ExpressionValue::
mergeToRowDestructive(RowValue & row)
{
    row.reserve(row.size() + rowLength());

    auto onSubexpr = [&] (ColumnName & columnName,
                          ExpressionValue & val)
        {
            val.appendToRow(columnName, row);
            return true;
        };

    forEachColumnDestructiveT(onSubexpr);
}

bool
ExpressionValue::
forEachAtom(const std::function<bool (const Coord & columnName,
                                      const Coord & prefix,
                                      const CellValue & val,
                                      Date ts) > & onAtom,
            const Coord & prefix) const
{
    switch (type_) {
    case ROW: {
        for (auto & col: *row_) {

            const ExpressionValue & val = std::get<1>(col);

            if (val.type_ == NONE) {
                if (!onAtom(std::get<0>(col), prefix, CellValue(), val.getEffectiveTimestamp()))
                    return false;
            }
            else if (val.type_ == ATOM) {
                if (!onAtom(std::get<0>(col), prefix, val.getAtom(), val.getEffectiveTimestamp()))
                    return false;
            }
            else {
                if (!val.forEachAtom(onAtom, prefix + std::get<0>(col)))
                    return false;
            }
        }
        return true;
    }
    case STRUCT: {
        for (unsigned i = 0;  i < struct_->length();  ++i) {
            if (!onAtom(struct_->columnName(i), prefix, struct_->value(i), ts_))
                return false;
        }
        return true;
    }
    case EMBEDDING: {
        auto onCol = [&] (ColumnName & columnName, CellValue & val)
            {
                return onAtom(columnName, prefix, val, ts_);
            };
        
        return embedding_->forEachColumn(onCol);
    }
    case NONE: {
        return onAtom(Coord(), prefix, CellValue(), ts_);
    }
    case ATOM: {
        return onAtom(Coord(), prefix, cell_, ts_);
    }
    }

    throw HttpReturnException(500, "Unknown expression type",
                              "expression", *this,
                              "type", (int)type_);
}

bool
ExpressionValue::
forEachSubexpression(const std::function<bool (const Coord & columnName,
                                               const Coord & prefix,
                                               const ExpressionValue & val)>
                         & onSubexpression,
                     const Coord & prefix) const
{
    switch (type_) {
    case ROW: {
        for (auto & col: *row_) {
            const ExpressionValue & val = std::get<1>(col);
            if (!onSubexpression(std::get<0>(col), prefix, val))
                return false;
        }
        return true;
    }
    case STRUCT: {
        for (unsigned i = 0;  i < struct_->length();  ++i) {
            ExpressionValue val(struct_->value(i), ts_);
            if (!onSubexpression(struct_->columnName(i), prefix, val))
                return false;
        }
        return true;
    }
    case EMBEDDING: {
        throw ML::Exception("Not implemented: forEachSubexpression for Embedding value");
    }
    case NONE: {
        return onSubexpression(Coord(), prefix, ExpressionValue::null(ts_));
    }
    case ATOM: {
        return onSubexpression(Coord(), prefix, ExpressionValue(cell_, ts_));
    }
    }

    throw HttpReturnException(500, "Unknown expression type",
                              "expression", *this,
                              "type", (int)type_);
}

bool
ExpressionValue::
forEachColumnDestructive(const std::function<bool (Coord & columnName, ExpressionValue & val)>
                                & onSubexpression) const
{
    return forEachColumnDestructiveT(onSubexpression);
}

template<typename Fn>
bool
ExpressionValue::
forEachColumnDestructiveT(Fn && onSubexpression) const
{
    switch (type_) {
    case ROW: {
        if (row_.unique()) {
            // We have the only reference in the shared pointer, AND we have
            // a non-const version of that expression.  This means that it
            // should be thread-safe to break constness and steal the result,
            // as otherwise we would have two references from different threads
            // with at least one non-const, which breaks thread safety.

            for (auto & col: const_cast<Row &>(*row_)) {
                ExpressionValue val(std::move(std::get<1>(col)));
                Coord columnName(std::move(std::get<0>(col)));
                if (!onSubexpression(columnName, val))
                    return false;
            }
        }
        else {
            for (auto & col: *row_) {
                ExpressionValue val = std::get<1>(col);
                Coord columnName = std::get<0>(col);
                if (!onSubexpression(columnName, val))
                    return false;
            }
        }
        return true;
    }
    case STRUCT: {
        if (struct_.unique()) {
            // See same comment above
            Struct & s = const_cast<Struct &>(*struct_);

            for (unsigned i = 0;  i < s.length();  ++i) {
                ExpressionValue val(std::move(s.moveValue(i)), ts_);
                Coord columnName = s.columnName(i);
                if (!onSubexpression(columnName, val))
                    return false;
            }

        }
        else {
            for (unsigned i = 0;  i < struct_->length();  ++i) {
                ExpressionValue val(struct_->value(i), ts_);
                Coord columnName = struct_->columnName(i);
                if (!onSubexpression(columnName, val))
                    return false;
            }
        }
        return true;
    }
    case EMBEDDING: {
        auto onCol = [&] (ColumnName & columnName, CellValue & val)
            {
                ExpressionValue eval(std::move(val), ts_);
                return onSubexpression(columnName, eval);
            };
        
        return embedding_->forEachColumn(onCol);
    }
    case NONE:
    case ATOM:
        throw HttpReturnException(500, "Expected row expression",
                                  "expression", *this,
                                  "type", (int)type_);
    }
    throw HttpReturnException(500, "Unknown expression type",
                              "expression", *this,
                              "type", (int)type_);
}

ExpressionValue::Row
ExpressionValue::
getFiltered(const VariableFilter & filter /*= GET_LATEST*/) const
{
    ExcAssertEqual(type_, ROW);

    //if (filter == GET_ALL)
    //    return *row_;

    std::function<bool(const ExpressionValue&, const ExpressionValue&)> filterFn = [](const ExpressionValue& left, const ExpressionValue& right){return false;};

     switch (filter) {
        case GET_ANY_ONE:
            //default is fine
            break;
        case GET_EARLIEST:
            filterFn = [](const ExpressionValue& left, const ExpressionValue& right){
                return right.isEarlier(left.getEffectiveTimestamp(), left);
            };
            break;
        case GET_LATEST:
            filterFn = [](const ExpressionValue& left, const ExpressionValue& right){
               return right.isLater(right.getEffectiveTimestamp(), left);
            };
            break;
        case GET_ALL: //optimized above
        default:
            throw HttpReturnException(500, "Unexpected filter");
     }

    //Remove any duplicated columns according to the filter
    std::unordered_map<ColumnName, ExpressionValue> values;
    for (auto & col: *row_) {
        Coord columnName = std::get<0>(col);
        auto iter = values.find(columnName);
        if (iter != values.end()) {
            const ExpressionValue& val = std::get<1>(col);
            if (filterFn(iter->second, val)) {
                iter->second = val;
            }
        }
        else {
            values.insert({columnName, std::get<1>(col)});
        }

        ExpressionValue val = std::get<1>(col);
    }

    //re-flatten to row
    Row output;
    for (auto & c : values)
        output.emplace_back(std::move(c.first), std::move(c.second));

    return output;

}

void
ExpressionValue::
filterRow(const VariableFilter & filter)
{
    Row row = getFiltered(filter);
    row_.reset(new Row(std::move(row)));

}

bool
ExpressionValue::
joinColumns(const ExpressionValue & val1,
            const ExpressionValue & val2,
            const OnMatchingColumn & onMatchingColumn,
            Outer outer)
{
    // TODO: we don't *at all* handle multiple versions of the current
    // column here.
    bool outerLeft = (outer == OUTER || outer == OUTER_LEFT);
    bool outerRight = (outer == OUTER || outer == OUTER_RIGHT);

    RowValue row1, row2;
    val1.appendToRow(ColumnName(), row1);
    val2.appendToRow(ColumnName(), row2);

    if (row1.size() == row2.size()) {
        bool matchingNames = true;

        // Assume they have the same keys and exactly one of each; if not we
        // have to restart
        ML::Lightweight_Hash_Set<uint64_t> colsFound;

        for (size_t i = 0;  i < row1.size(); ++i) {
            const ColumnName & col1 = std::get<0>(row1[i]);
            const ColumnName & col2 = std::get<0>(row2[i]);
            if (col1 != col2) {
                // non-matching column name
                matchingNames = false;
                break;
            }
            if (colsFound.insert(col1.hash()).second == false) {
                // duplicate column name
                matchingNames = false;
                break;
            }
        }

        if (matchingNames) {
            // Easy case: one single value of each one
            for (size_t i = 0;  i < row1.size(); ++i) {
                const ColumnName & col = std::get<0>(row1[i]);

                std::pair<CellValue, Date>
                    val1(std::get<1>(row1[i]), std::get<2>(row1[i]));
                std::pair<CellValue, Date>
                    val2(std::get<1>(row2[i]), std::get<2>(row2[i]));

                if (!onMatchingColumn(col, &val1, &val2, 1, 1))
                    return false;
            }
            return true;
        }
    }

    // Names don't match or there are duplicates.  We need to sort them and
    // iterate across batches of matching entries.

    std::sort(row1.begin(), row1.end());
    std::sort(row2.begin(), row2.end());

    auto it1 = row1.begin(), end1 = row1.end();
    auto it2 = row2.begin(), end2 = row2.end();

    while (it1 != end1 && it2 != end2) {
        const ColumnName & col1 = std::get<0>(*it1);
        const ColumnName & col2 = std::get<0>(*it2);
        
        const ColumnName & minCol = col1 < col2 ? col1 : col2;

        cerr << "doing columns " << col1 << " and " << col2 << endl;

        // If we don't have a match on each side, check for the
        // outer condition and if it doesn't match, continue on later.
        if (minCol != col1 && !outerLeft) {
            while (it2 != end2 && std::get<0>(*it2) < col1)
                ++it2;
            continue;
        }
        if (minCol != col2 && !outerRight) {
            while (it1 != end1 && std::get<0>(*it1) < col2)
                ++it2;
            continue;
        }

        std::vector<std::pair<CellValue, Date> > lvals, rvals;

        while (it1 != end1 && std::get<0>(*it1) == minCol) {
            lvals.emplace_back(std::get<1>(*it1), std::get<2>(*it1));
            ++it1;
        }
        while (it2 != end2 && std::get<0>(*it2) == minCol) {
            rvals.emplace_back(std::get<1>(*it2), std::get<2>(*it2));
            ++it2;
        }
        
        // Present the examples
        if (!onMatchingColumn(minCol, &lvals[0], &rvals[0],
                              lvals.size(), rvals.size()))
            return false;
    }
    
    while (outerLeft && it1 != end1) {
        const ColumnName & col1 = std::get<0>(*it1);

        std::vector<std::pair<CellValue, Date> > lvals, rvals;

        while (it1 != end1 && std::get<0>(*it1) == col1) {
            lvals.emplace_back(std::get<1>(*it1), std::get<2>(*it1));
            ++it1;
        }
        
        // Present the examples
        if (!onMatchingColumn(col1, &lvals[0], &rvals[0],
                              lvals.size(), rvals.size()))
            return false;
    }

    while (outerRight && it2 != end2) {
        const ColumnName & col2 = std::get<0>(*it2);

        std::vector<std::pair<CellValue, Date> > lvals, rvals;

        while (it2 != end2 && std::get<0>(*it2) == col2) {
            rvals.emplace_back(std::get<1>(*it2), std::get<2>(*it2));
            ++it2;
        }

        // Present the examples
        if (!onMatchingColumn(col2, &lvals[0], &rvals[0],
                              lvals.size(), rvals.size()))
            return false;
    }

    return true;
}

std::pair<bool, Date>
ExpressionValue::
hasKey(const Utf8String & key) const
{
    switch (type_) {
    case NONE:
    case ATOM:
        return { false, Date::negativeInfinity() };
    case ROW: 
    case STRUCT:
    case EMBEDDING: {
        // TODO: for Embedding, we can do much, much better
        Date outputDate = Date::negativeInfinity();
        auto onExpr = [&] (const Coord & columnName,
                           const Coord & prefix,
                           const ExpressionValue & val)
            {
                if (columnName.toUtf8String() == key) {
                    outputDate = val.getEffectiveTimestamp();
                    return false;
                }

                return true;
            };
     
        // Can't be with next line due to sequence points
        bool result = !forEachSubexpression(onExpr);
        return { result, outputDate };
    }
#if 0
    case EMBEDDING: {
        // Look for a key of the form [x,y,z]
        int indexes[10];
        int n = -1;
        int64_t current = 0;

        for (char32_t c: key) {
            if (n == -1) {
                if (c != '[')
                    return { false, Date::negativeInfinity() };
                n = 0;
            }
            if (c == 0) {
            }
        }
    }
#endif
    }

    throw HttpReturnException(500, "Unknown expression type",
                              "expression", *this,
                              "type", (int)type_);
}

std::pair<bool, Date>
ExpressionValue::
hasValue(const ExpressionValue & val) const
{
    switch (type_) {
    case NONE:
    case ATOM:
        return { false, Date::negativeInfinity() };
    case ROW: 
    case STRUCT:
    case EMBEDDING: {
        // TODO: for embedding, we can do much, much better
        Date outputDate = Date::negativeInfinity();
        auto onExpr = [&] (const Coord & columnName,
                           const Coord & prefix,
                           const ExpressionValue & value)
            {
                if (val == value) {
                    outputDate = value.getEffectiveTimestamp();
                    return false;
                }

                return true;
            };
        
        // Can't be with next line due to sequence points
        bool result = !forEachSubexpression(onExpr);
        return { result, outputDate };
    }
    }

    throw HttpReturnException(500, "Unknown expression type",
                              "expression", *this,
                              "type", (int)type_);
}

size_t
ExpressionValue::
hash() const
{
    switch (type_) {
    case NONE:
        return CellValue().hash();
    case ATOM:
        return cell_.hash();
    case ROW:
    case STRUCT:
    case EMBEDDING:
        // TODO: a more reasonable speed in hashing
        return jsonHash(jsonEncode(*this));
    }
    throw HttpReturnException(500, "Unknown expression type",
                              "expression", *this,
                              "type", (int)type_);
}

void
ExpressionValue::
initInt(int64_t intValue, Date ts)
{
    type_ = NONE;
    setAtom(intValue, ts);
}

void
ExpressionValue::
initUInt(uint64_t intValue, Date ts)
{
    type_ = NONE;
    setAtom(intValue, ts);
}

void
ExpressionValue::
initRow(Row value) noexcept
{
    initRow(std::make_shared<Row>(std::move(value)));
}

void
ExpressionValue::
initRow(std::shared_ptr<const Row> value) noexcept
{
    ExcAssertEqual(type_, NONE);
    ts_ = Date();
    if (value->size() == 0) {
        ts_ = Date::notADate();
    }
    else {
        for (auto& r : *value) {
            ts_ = std::max(std::get<1>(r).getEffectiveTimestamp(), ts_);
        }
    }

    new (storage_) std::shared_ptr<const Row>(std::move(value));
    type_ = ROW;
}

static const CellValue EMPTY_CELL;

const CellValue &
ExpressionValue::
getAtom() const
{
    if (type_ == NONE)
        return EMPTY_CELL;
    ExcAssertEqual(type_, ATOM);
    return cell_;
}

CellValue
ExpressionValue::
stealAtom()
{
    if (type_ == NONE)
        return CellValue();
    ExcAssertEqual(type_, ATOM);
    return std::move(cell_);
}

CellValue
ExpressionValue::
coerceToAtom() const
{
    switch (type_) {
    case NONE:
        return EMPTY_CELL;
    case ATOM:
        return cell_;
    case ROW:
        ExcAssertEqual(row_->size(), 1);
        return std::get<1>((*row_)[0]).getAtom();
    case STRUCT:
        ExcAssertEqual(struct_->length(), 1);
        return CellValue(struct_->value(0));
    case EMBEDDING:
        ExcAssertEqual(embedding_->length(), 1);
        return embedding_->getValue(0);
    }

    throw HttpReturnException(500, "coerceToAtom: unknown expression type");

    return EMPTY_CELL;
}

const ExpressionValue::Row &
ExpressionValue::
getRow() const
{
    ExcAssertEqual(type_, ROW);
    return *row_;
}

ExpressionValue::Row
ExpressionValue::
stealRow()
{
    ExcAssertEqual(type_, ROW);
    type_ = NONE;
    return std::move(*row_);
}

CellValue
ExpressionValue::
coerceToString() const
{
    if (type_ == NONE)
        return CellValue();
    return toUtf8String();
}

CellValue
ExpressionValue::
coerceToInteger() const
{
    if (type_ != ATOM)
        return CellValue();
    return cell_.coerceToInteger();
}

CellValue
ExpressionValue::
coerceToNumber() const
{
    if (type_ != ATOM)
        return CellValue();
    return cell_.coerceToNumber();
}

CellValue
ExpressionValue::
coerceToBoolean() const
{
    if (type_ != ATOM)
        return CellValue();
    return cell_.coerceToBoolean();
}

CellValue
ExpressionValue::
coerceToTimestamp() const
{
    if (type_ != ATOM)
        return CellValue();
    return cell_.coerceToTimestamp();
}

CellValue
ExpressionValue::
coerceToBlob() const
{
    if (type_ != ATOM)
        return CellValue();
    return cell_.coerceToBlob();
}

void
ExpressionValue::
setAtom(CellValue value, Date ts)
{
    initAtom(std::move(value), ts);
}

vector<std::pair<ColumnName, CellValue> > asRow(const ExpressionValue & expr) {
    vector<std::pair<ColumnName, CellValue> > row;
    auto onAtom = [&] (const ColumnName & columnName,
                       const ColumnName & prefix,
                       const CellValue & val,
                       Date ts)
        {
            row.emplace_back(columnName, val);
            return true;
        };
    expr.forEachAtom(onAtom);
    return row;
}

template <typename T, template <typename> class Compare> 
typename Compare<T>::result_type 
compare_t(const ExpressionValue & left, const ExpressionValue & right)
{
    T leftRow = asRow(left);
    T rightRow = asRow(right);
    return ML::compare_sorted(leftRow, rightRow, Compare<T>());
}

int
ExpressionValue::
compare(const ExpressionValue & other) const
{
    if (type_ < other.type_)
        return -1;
    else if (type_ > other.type_)
        return 1;
    
    switch (type_) {
    case NONE: return 0;
    case ATOM:
        //cerr << "getAtom() 1 = " << getAtom() << endl;
        //cerr << "getAtom() 2 = " << other.getAtom() << endl;
        //cerr << "atom compare returned " << getAtom().compare(other.getAtom()) << endl;
        //cerr << "reverse atom compare returned " << other.getAtom().compare(getAtom()) << endl;
        return cell_.compare(other.cell_);
    case ROW: {
        auto leftRow = getRow();
        auto rightRow = other.getRow();
        return ML::compare_sorted(leftRow, rightRow, ML::compare<Row>());
    }
    case STRUCT: {
        return compare_t<vector<pair<ColumnName, CellValue> >, ML::compare>(*this, other);
    }
    case EMBEDDING: {
        return compare_t<vector<pair<ColumnName, CellValue> >, ML::compare>(*this, other);
    }
    }
    
    throw HttpReturnException(400, "unknown ExpressionValue type");
}

bool
ExpressionValue::
operator == (const ExpressionValue & other) const
{
    if (type_ != other.type_)
        return false;
    switch (type_) {
    case NONE: return true;
    case ATOM: return cell_ == other.cell_;
    case ROW: {
        auto leftRow = getRow();
        auto rightRow = other.getRow();
        return ML::compare_sorted(leftRow, rightRow, std::equal_to<Row>());
    }
    case STRUCT:
    case EMBEDDING: {
        return compare_t<vector<pair<ColumnName, CellValue> >, equal_to>(*this, other);
    }
    }
    throw HttpReturnException(400, "unknown ExpressionValue type " + to_string(type_));
}

bool
ExpressionValue::
operator <  (const ExpressionValue & other) const
{
    if (type_ < other.type_)
        return true;
    if (type_ > other.type_)
        return false;

    switch (type_) {
    case NONE: return false;
    case ATOM: return cell_ < other.cell_;
    case ROW:  {
        auto leftRow = getRow();
        auto rightRow = other.getRow();
        return ML::compare_sorted(leftRow, rightRow, std::less<Row>());
    }
    case STRUCT: {
        return compare_t<vector<pair<ColumnName, CellValue> >, less>(*this, other);
    }
    case EMBEDDING: {
        return compare_t<vector<pair<ColumnName, CellValue> >, less>(*this, other);
    }
    }
    throw HttpReturnException(400, "unknown ExpressionValue type");
}

std::shared_ptr<ExpressionValueInfo>
ExpressionValue::
getSpecializedValueInfo() const
{
    switch (type_) {
    case NONE:
        return std::make_shared<EmptyValueInfo>();
    case ATOM:
        // TODO: specialize for concrete type
        return std::make_shared<AtomValueInfo>();
    case ROW:
        // TODO: specialize for concrete value.  Currently we just say
        // "it's a row with some values we don't know about yet"
        return std::make_shared<RowValueInfo>(vector<KnownColumn>(), SCHEMA_OPEN);
    case STRUCT:
        throw ML::Exception("struct getSpecializedValueInfo not done");
    case EMBEDDING:
        throw ML::Exception("embedding getSpecializedValueInfo not done");
    }
    throw HttpReturnException(400, "unknown ExpressionValue type");
}

using Datacratic::getDefaultDescriptionShared;

namespace {
auto cellDesc = getDefaultDescriptionShared((CellValue *)0);
auto rowDesc = getDefaultDescriptionShared((ExpressionValue::Row *)0);
auto noTsDesc = getExpressionValueDescriptionNoTimestamp();
auto dateDesc = getDefaultDescriptionShared((Date *)0);
} // file scope

void
ExpressionValue::
extractJson(JsonPrintingContext & context) const
{
    switch (type_) {
    case ExpressionValue::NONE:
        context.writeNull();
        return;

    case ExpressionValue::ATOM:
        cellDesc->printJsonTyped(&cell_, context);
        return;

    case ExpressionValue::ROW: {
        Json::Value output(Json::objectValue);

        for (auto & r: *row_)
            output[std::get<0>(r).toUtf8String()] = std::get<1>(r).extractJson();

        context.writeJson(output);
        return;
    }
    case ExpressionValue::STRUCT: {
        Json::Value output(Json::arrayValue);

        if (!struct_->columnNames) {
            for (unsigned i = 0;  i < struct_->length();  ++i)
                output[i] = jsonEncode(struct_->value(i));
        }
        else if (struct_->length() > 0
                 && struct_->columnNames->at(0) == ColumnName(0)) {
            // Assume it's an array
            
            for (unsigned i = 0;  i < struct_->length();  ++i) {
                output[i] = jsonEncode(struct_->value(i));
                if (struct_->columnNames->at(i) != i) {
                    // Not really an array or embedding...
                    output = Json::Value();
                    break;
                }
            }
        }

        if (!output.isNull()) {
            context.writeJson(output);
            return;
        }

        output = Json::Value(Json::objectValue);

        for (unsigned i = 0;  i < struct_->length();  ++i) {
            output[struct_->columnName(i).toUtf8String()]
                = jsonEncode(struct_->value(i));
        }

        context.writeJson(output);
        return;
    }
    case ExpressionValue::EMBEDDING: {
        throw HttpReturnException(500, "extractJson EMBEDDING: not impl");
    }
    }
    throw HttpReturnException(400, "unknown ExpressionValue type");
}

Json::Value
ExpressionValue::
extractJson() const
{
    StructuredJsonPrintingContext context;
    extractJson(context);
    return context.output;
}

/** Value description that includes the timestamp.  It serializes as an array with 
    [ value, timestamp ].
*/
struct ExpressionValueDescription
    : public ValueDescriptionT<ExpressionValue> {
    virtual void parseJsonTyped(ExpressionValue * val,
                                JsonParsingContext & context) const;
    virtual void printJsonTyped(const ExpressionValue * val,
                                JsonPrintingContext & context) const;
    virtual bool isDefaultTyped(const ExpressionValue * val) const;
};

/** Value description not including the timestamp.  It serializes as just the
    value.  When reading back, the timestamp will be set to NaD.
*/
struct ExpressionValueDescriptionNoTimestamp
    : public ValueDescriptionT<ExpressionValue> {
    virtual void parseJsonTyped(ExpressionValue * val,
                                JsonParsingContext & context) const;
    virtual void printJsonTyped(const ExpressionValue * val,
                                JsonPrintingContext & context) const;
    virtual bool isDefaultTyped(const ExpressionValue * val) const;
};

std::shared_ptr<ValueDescriptionT<ExpressionValue> >
getExpressionValueDescriptionNoTimestamp()
{
    return std::make_shared<ExpressionValueDescriptionNoTimestamp>();
}

void
ExpressionValueDescription::
parseJsonTyped(ExpressionValue * val,
               JsonParsingContext & context) const
{
    int el = 0;
    auto onElement = [&] ()
        {
            if (el == 0)
                noTsDesc->parseJsonTyped(val, context);
            else if (el == 1)
                dateDesc->parseJsonTyped(&val->ts_, context);
            else context.exception("expected 2 element array");

            ++el;
        };

    context.forEachElement(onElement);
    
    if (el != 2)
        context.exception("expected 2 element array");
}

void
ExpressionValueDescription::
printJsonTyped(const ExpressionValue * val,
               JsonPrintingContext & context) const
{
    context.startArray(2);
    context.newArrayElement();
    noTsDesc->printJsonTyped(val, context);
    context.newArrayElement();
    dateDesc->printJsonTyped(&val->ts_, context);
    context.endArray();
}

bool
ExpressionValueDescription::
isDefaultTyped(const ExpressionValue * val) const
{
    return false;
}

void
ExpressionValueDescriptionNoTimestamp::
parseJsonTyped(ExpressionValue * val,
               JsonParsingContext & context) const
{
    Date ts = Date::notADate();
        
    if (context.isNull()) {
        context.expectNull();
        *val = ExpressionValue(nullptr, ts);
    }
    else if (context.isString()) {
        *val = ExpressionValue(context.expectStringUtf8(), ts);
    }
    else if (context.isArray()) {
        Json::Value j = context.expectJson();
        *val = ExpressionValue(j, ts);
    }
    else if (context.isObject()) {
        Json::Value j = context.expectJson();
        *val = ExpressionValue(j, ts);
    }
    else if (context.isBool()) {
        *val = ExpressionValue(context.expectBool(), ts);
    }
    else if (context.isUnsigned()) {
        *val = ExpressionValue(context.expectUnsignedLongLong(), ts);
    }
    else if (context.isInt()) {
        *val = ExpressionValue(context.expectLongLong(), ts);
    }
    else {
        *val = ExpressionValue(context.expectDouble(), ts);
    }
}

void
ExpressionValueDescriptionNoTimestamp::
printJsonTyped(const ExpressionValue * val,
               JsonPrintingContext & context) const
{
    switch (val->type_) {
    case ExpressionValue::NONE: context.writeNull();  return;
    case ExpressionValue::ATOM: cellDesc->printJsonTyped(&val->cell_, context);  return;
    case ExpressionValue::ROW:
        rowDesc->printJsonTyped(val->row_.get(),   context);
        return;
    case ExpressionValue::STRUCT: {
        Json::Value output(Json::arrayValue);

        if (!val->struct_->columnNames) {
            for (unsigned i = 0;  i < val->struct_->length();  ++i)
                output[i] = jsonEncode(val->struct_->value(i));
        }
        else if (val->struct_->length() > 0
                 && val->struct_->columnName(0) == Coord(0)) {
            // Assume it's an array
            
            for (unsigned i = 0;  i < val->struct_->length();  ++i) {
                output[i] = jsonEncode(val->struct_->value(i));
                if (val->struct_->columnName(i) != i) {
                    // Not really an array or embedding...
                    output = Json::Value();
                    return;
                }
            }
        }

        if (!output.isNull()) {
            context.writeJson(output);
            return;
        }

        output = Json::Value(Json::objectValue);

        for (unsigned i = 0;  i < val->struct_->length();  ++i) {
            output[val->struct_->columnName(i).toUtf8String()]
                = jsonEncode(val->struct_->value(i));
        }

        context.writeJson(output);
        return;
    }
    case ExpressionValue::EMBEDDING: {
        val->embedding_->writeJson(context);
        return;
    }
    }
    throw HttpReturnException(400, "unknown ExpressionValue type");
}

bool
ExpressionValueDescriptionNoTimestamp::
isDefaultTyped(const ExpressionValue * val) const
{
    return val->empty();
}

// Get a value description for values
DEFINE_VALUE_DESCRIPTION_NS(ExpressionValue, ExpressionValueDescription);

std::ostream & operator << (std::ostream & stream, const ExpressionValue & val)
{
    return stream << jsonEncode(val);
}


/*****************************************************************************/
/* TEMPLATE INSTANTIATIONS                                                   */
/*****************************************************************************/

template class ExpressionValueInfoT<float>;
template class ExpressionValueInfoT<double>;
template class ExpressionValueInfoT<CellValue>;
template class ExpressionValueInfoT<std::string>;
template class ExpressionValueInfoT<Utf8String>;
template class ExpressionValueInfoT<std::vector<unsigned char> >;
template class ExpressionValueInfoT<int64_t>;
template class ExpressionValueInfoT<uint64_t>;
template class ExpressionValueInfoT<char>;
template class ExpressionValueInfoT<Date>;
template class ScalarExpressionValueInfoT<float>;
template class ScalarExpressionValueInfoT<double>;
template class ScalarExpressionValueInfoT<CellValue>;
template class ScalarExpressionValueInfoT<std::string>;
template class ScalarExpressionValueInfoT<Utf8String>;
template class ScalarExpressionValueInfoT<std::vector<unsigned char> >;
template class ScalarExpressionValueInfoT<int64_t>;
template class ScalarExpressionValueInfoT<uint64_t>;
template class ScalarExpressionValueInfoT<char>;
template class ScalarExpressionValueInfoT<Date>;

template class ExpressionValueInfoT<RowValue>;
template class ExpressionValueInfoT<ExpressionValue>;
template class ExpressionValueInfoT<ML::distribution<double, std::vector<double> > >;


/*****************************************************************************/
/* SEARCH ROW                                                                */
/*****************************************************************************/

template<typename Key>
const ExpressionValue *
doSearchRow(const std::vector<std::tuple<Key, CellValue, Date> > & columns,
            const Key & key,
            const VariableFilter & filter,
            ExpressionValue & storage)
{
    int index = -1;

    //cerr << "doSearchRow columns " << jsonEncode(columns) << " key " << key << endl;

    switch (filter) {
    case GET_ANY_ONE:
        for (unsigned i = 0;  i < columns.size();  ++i) {
            const auto & c = columns[i];
            if (std::get<0>(c) == key) {
                index = i;
                break;
            }
        }
        break;
        
    case GET_EARLIEST: {
        Date foundDate;

        for (unsigned i = 0;  i < columns.size();  ++i) {
            const auto & c = columns[i];
            
            if (std::get<0>(c) == key) {
                if (index == -1
                    || std::get<2>(c) < foundDate
                    || (std::get<2>(c) == foundDate
                        && std::get<1>(c) < std::get<1>(columns[index]))) {
                    index = i;
                    foundDate = std::get<2>(c);
                }
            }
        }
        break;
    }

    case GET_LATEST: {
        Date foundDate;

        for (unsigned i = 0;  i < columns.size();  ++i) {
            const auto & c = columns[i];
            
            if (std::get<0>(c) == key) {
                if (index == -1
                    || std::get<2>(c) > foundDate
                    || (std::get<2>(c) == foundDate
                        && std::get<1>(c) < std::get<1>(columns[index]))) {
                    index = i;
                    foundDate = std::get<2>(c);
                }
            }
        }
        break;
    }

    case GET_ALL: {
        RowValue row;
        #if 0
        for (unsigned i = 0;  i < columns.size();  ++i) {
            const auto & c = columns[i];
            
            if (std::get<0>(c) == key) {
                row.push_back(c);
            }
        }
        
        return &(storage = std::move(ExpressionValue(std::move(row))));
        #endif
        throw HttpReturnException(500, "GET_ALL not implemented for datasets");
    }

    default:
        throw HttpReturnException(500, "Unknown GET_ALL not implemented for datasets");
    }

    if (index == -1)
        return nullptr;
    
    return &(storage = std::move(ExpressionValue(std::get<1>(columns[index]),
                                                 std::get<2>(columns[index]))));
}

#if 0
const ExpressionValue *
searchRow(const std::vector<std::tuple<ColumnHash, CellValue, Date> > & columns,
          const ColumnHash & key,
          const VariableFilter & filter,
          ExpressionValue & storage)
{
    return doSearchRow(columns, key, filter, storage);
}

const ExpressionValue *
searchRow(const std::vector<std::tuple<ColumnHash, CellValue, Date> > & columns,
          const ColumnName & key,
          const VariableFilter & filter,
          ExpressionValue & storage)
{
    return doSearchRow(columns, ColumnHash(key), filter, storage);
}
#endif
const ExpressionValue *
searchRow(const std::vector<std::tuple<ColumnName, CellValue, Date> > & columns,
          const ColumnName & key,
          const VariableFilter & filter,
          ExpressionValue & storage)
{
    return doSearchRow(columns, key, filter, storage);
}

template<typename Key>
const ExpressionValue *
doSearchRow(const std::vector<std::tuple<Key, ExpressionValue> > & columns,
            const Key & key,
            const VariableFilter & filter,
            ExpressionValue & storage)
{
    int index = -1;

    switch (filter) {
    case GET_ANY_ONE:
        for (unsigned i = 0;  i < columns.size();  ++i) {
            const auto & c = columns[i];
            if (std::get<0>(c) == key) {
                index = i;
                break;
            }
        }
        break;
        
    case GET_EARLIEST: {
        Date foundDate;

        for (unsigned i = 0;  i < columns.size();  ++i) {
            const auto & c = columns[i];
            
            if (std::get<0>(c) == key) {
                const ExpressionValue & v = std::get<1>(c);
                Date ts = v.getEffectiveTimestamp();
                if (index == -1 || v.isEarlier(ts, std::get<1>(columns[index]))){
                    index = i;
                    foundDate = ts;
                }
            }
        }
        break;
    }

    case GET_LATEST: {
        Date foundDate;

        for (unsigned i = 0;  i < columns.size();  ++i) {
            const auto & c = columns[i];
            
            if (std::get<0>(c) == key) {
                const ExpressionValue & v = std::get<1>(c);
                Date ts = v.getEffectiveTimestamp();
                if (index == -1 || v.isLater(ts, std::get<1>(columns[index]))){
                    index = i;
                    foundDate = ts;
                }
            }
        }

        break;
    }

   case GET_ALL: {
        RowValue row;

        throw HttpReturnException(500, "GET_ALL not implemented for datasets");
    }

    default:
        throw HttpReturnException(500, "Unknown GET_ALL not implemented for datasets");
    }

    if (index == -1)
        return nullptr;
    
    return &std::get<1>(columns[index]);
}

const ExpressionValue *
searchRow(const std::vector<std::tuple<ColumnName, ExpressionValue> > & columns,
          const ColumnName & key,
          const VariableFilter & filter,
          ExpressionValue & storage)
{
    return doSearchRow(columns, key, filter, storage);
}


/*****************************************************************************/
/* NAMED ROW VALUE                                                           */
/*****************************************************************************/

DEFINE_STRUCTURE_DESCRIPTION(NamedRowValue);

NamedRowValueDescription::
NamedRowValueDescription()
{
    addField("rowName", &NamedRowValue::rowName, "Name of the row");
    addField("rowHash", &NamedRowValue::rowHash, "Hash of the row");
    addField("columns", &NamedRowValue::columns, "Columns active for this row");
}

#if 0
NamedRowValue::operator MatrixNamedRow() const
{
    MatrixNamedRow result;
    result.rowName = rowName;
    result.rowHash = rowHash;

    for (auto & c: columns) {
        const ColumnName & columnName = std::get<0>(c);
        const ExpressionValue & val = std::get<1>(c);
        val.appendToRow(columnName, result);
    }
    
    return result;
}
#endif

MatrixNamedRow
NamedRowValue::flattenDestructive()
{
    MatrixNamedRow result;

    result.rowName = std::move(rowName);
    result.rowHash = std::move(rowHash);

    for (auto & c: columns) {
        ColumnName & columnName = std::get<0>(c);
        ExpressionValue & val = std::get<1>(c);
        val.appendToRowDestructive(columnName, result.columns);
    }
    
    return result;
}

} // namespace MLDB
} // namespace Datacratic
