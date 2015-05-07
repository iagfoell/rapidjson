// Tencent is pleased to support the open source community by making RapidJSON available->
// 
// Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip-> All rights reserved->
//
// Licensed under the MIT License (the "License"); you may not use this file except
// in compliance with the License-> You may obtain a copy of the License at
//
// http://opensource->org/licenses/MIT
//
// Unless required by applicable law or agreed to in writing, software distributed 
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR 
// CONDITIONS OF ANY KIND, either express or implied-> See the License for the 
// specific language governing permissions and limitations under the License->

#ifndef RAPIDJSON_SCHEMA_H_
#define RAPIDJSON_SCHEMA_H_

#include "document.h"
#include <cmath> // HUGE_VAL, fmod

#if !defined(RAPIDJSON_SCHEMA_USE_STDREGEX) && (__cplusplus >=201103L || (defined(_MSC_VER) && _MSC_VER >= 1800))
#define RAPIDJSON_SCHEMA_USE_STDREGEX 1
#endif

#if RAPIDJSON_SCHEMA_USE_STDREGEX
#include <regex>
#endif

#if RAPIDJSON_SCHEMA_USE_STDREGEX // or some other implementation
#define RAPIDJSON_SCHEMA_HAS_REGEX 1
#else
#define RAPIDJSON_SCHEMA_HAS_REGEX 0
#endif

#if defined(__GNUC__)
RAPIDJSON_DIAG_PUSH
RAPIDJSON_DIAG_OFF(effc++)
RAPIDJSON_DIAG_OFF(float-equal)
#endif

RAPIDJSON_NAMESPACE_BEGIN

enum SchemaType {
    kNullSchemaType,
    kBooleanSchemaType,
    kObjectSchemaType,
    kArraySchemaType,
    kStringSchemaType,
    kNumberSchemaType,
    kIntegerSchemaType,
    kTotalSchemaType
};

template <typename Encoding>
class BaseSchema;

template <typename Encoding, typename OutputHandler, typename Allocator>
class GenericSchemaValidator;

template <typename Encoding>
struct SchemaValidatorArray {
    SchemaValidatorArray() : validators(), count() {}
    ~SchemaValidatorArray() {
        if (validators) {
            for (SizeType i = 0; i < count; i++)
                delete validators[i];
            delete[] validators;
        }
    }

    GenericSchemaValidator<Encoding, BaseReaderHandler<>, CrtAllocator>** validators;
    SizeType count;
};

template <typename Encoding>
struct BaseSchemaArray {
    BaseSchemaArray() : schemas(), count() {}
    ~BaseSchemaArray() {
        if (schemas) {
            for (SizeType i = 0; i < count; i++)
                delete schemas[i];
            delete[] schemas;
        }
    }

    BaseSchema<Encoding>** schemas;
    SizeType count;
};

template <typename Encoding>
struct SchemaValidationContext {
    SchemaValidationContext(const BaseSchema<Encoding>* s) : 
        schema(s), valueSchema(), multiTypeSchema(), notValidator(), objectDependencies(), inArray(false)
    {
    }

    ~SchemaValidationContext() {
        delete notValidator;
        delete[] objectDependencies;
    }

    const BaseSchema<Encoding>* schema;
    const BaseSchema<Encoding>* valueSchema;
    const BaseSchema<Encoding>* multiTypeSchema;
    SchemaValidatorArray<Encoding> allOfValidators;
    SchemaValidatorArray<Encoding> anyOfValidators;
    SchemaValidatorArray<Encoding> oneOfValidators;
    GenericSchemaValidator<Encoding, BaseReaderHandler<>, CrtAllocator>* notValidator;
    SizeType objectRequiredCount;
    SizeType arrayElementIndex;
    bool* objectDependencies;
    bool inArray;
};

template <typename Encoding>
class BaseSchema {
public:
    typedef typename Encoding::Ch Ch;
    typedef SchemaValidationContext<Encoding> Context;

    template <typename ValueType>
    BaseSchema(const ValueType& value) : 
        not_(),
        type_((1 << kTotalSchemaType) - 1), // typeless
        properties_(),
        additionalPropertySchema_(),
#if RAPIDJSON_SCHEMA_HAS_REGEX
        patternProperties_(),
        patternPropertyCount_(),
#endif
        propertyCount_(),
        requiredCount_(),
        minProperties_(),
        maxProperties_(SizeType(~0)),
        additionalProperty_(true),
        hasDependencies_(),
        itemsList_(),
        itemsTuple_(),
        itemsTupleCount_(),
        minItems_(),
        maxItems_(SizeType(~0)),
        additionalItems_(true),
#if RAPIDJSON_SCHEMA_USE_STDREGEX
        pattern_(),
#endif
        minLength_(0),
        maxLength_(~SizeType(0)),
        minimum_(-HUGE_VAL),
        maximum_(HUGE_VAL),
        multipleOf_(0),
        hasMultipleOf_(false),
        exclusiveMinimum_(false),
        exclusiveMaximum_(false)
    {
        typedef typename ValueType::ConstValueIterator ConstValueIterator;
        typedef typename ValueType::ConstMemberIterator ConstMemberIterator;

        if (!value.IsObject())
            return;

        if (const ValueType* v = GetMember(value, "type")) {
            type_ = 0;
            if (v->IsString())
                AddType(*v);
            else if (v->IsArray())
                for (ConstValueIterator itr = v->Begin(); itr != v->End(); ++itr)
                    AddType(*itr);
        }

        if (const ValueType* v = GetMember(value, "enum"))
            if (v->IsArray() && v->Size() > 0)
                enum_.CopyFrom(*v, allocator_);

        AssigIfExist(allOf_, value, "allOf");
        AssigIfExist(anyOf_, value, "anyOf");
        AssigIfExist(oneOf_, value, "oneOf");

        if (const ValueType* v = GetMember(value, "not"))
            not_ = new BaseSchema<Encoding>(*v);

        // Object
        if (const ValueType* v = GetMember(value, "properties"))
            if (v->IsObject()) {
                properties_ = new Property[v->MemberCount()];
                propertyCount_ = 0;
                for (ConstMemberIterator itr = v->MemberBegin(); itr != v->MemberEnd(); ++itr) {
                    properties_[propertyCount_].name.SetString(itr->name.GetString(), itr->name.GetStringLength(), allocator_);
                    properties_[propertyCount_].schema = new BaseSchema(itr->value);
                    propertyCount_++;
                }
            }

#if RAPIDJSON_SCHEMA_HAS_REGEX
        if (const ValueType* v = GetMember(value, "patternProperties")) {
            patternProperties_ = new PatternProperty[v->MemberCount()];
            patternPropertyCount_ = 0;

            for (ConstMemberIterator itr = v->MemberBegin(); itr != v->MemberEnd(); ++itr) {
                patternProperties_[patternPropertyCount_].pattern = CreatePattern(itr->name);
                patternProperties_[patternPropertyCount_].schema = new BaseSchema<Encoding>(itr->value);    // TODO: Check error
                patternPropertyCount_++;
            }
        }
#endif

        if (const ValueType* v = GetMember(value, "required"))
            if (v->IsArray())
                for (ConstValueIterator itr = v->Begin(); itr != v->End(); ++itr)
                    if (itr->IsString()) {
                        SizeType index;
                        if (FindPropertyIndex(*itr, &index)) {
                            properties_[index].required = true;
                            requiredCount_++;
                        }
                    }

        if (const ValueType* v = GetMember(value, "dependencies"))
            if (v->IsObject()) {
                hasDependencies_ = true;
                for (ConstMemberIterator itr = v->MemberBegin(); itr != v->MemberEnd(); ++itr) {
                    SizeType sourceIndex;
                    if (FindPropertyIndex(itr->name, &sourceIndex)) {
                        if (itr->value.IsArray()) {
                            properties_[sourceIndex].dependencies = new bool[propertyCount_];
                            std::memset(properties_[sourceIndex].dependencies, 0, sizeof(bool)* propertyCount_);
                            for (ConstValueIterator targetItr = itr->value.Begin(); targetItr != itr->value.End(); ++targetItr) {
                                SizeType targetIndex;
                                if (FindPropertyIndex(*targetItr, &targetIndex))
                                    properties_[sourceIndex].dependencies[targetIndex] = true;
                            }
                        }
                        else if (itr->value.IsObject()) {
                            // TODO
                        }
                    }
                }
            }

        if (const ValueType* v = GetMember(value, "additionalProperties")) {
            if (v->IsBool())
                additionalProperty_ = v->GetBool();
            else if (v->IsObject())
                additionalPropertySchema_ = new BaseSchema<Encoding>(*v);
        }

        AssignIfExist(minProperties_, value, "minProperties");
        AssignIfExist(maxProperties_, value, "maxProperties");

        // Array
        if (const ValueType* v = GetMember(value, "items")) {
            if (v->IsObject()) // List validation
                itemsList_ = new BaseSchema<Encoding>(*v);
            else if (v->IsArray()) { // Tuple validation
                itemsTuple_ = new BaseSchema<Encoding>*[v->Size()];
                for (ConstValueIterator itr = v->Begin(); itr != v->End(); ++itr)
                    itemsTuple_[itemsTupleCount_++] = new BaseSchema<Encoding>(*itr);
            }
        }

        AssignIfExist(minItems_, value, "minItems");
        AssignIfExist(maxItems_, value, "maxItems");
        AssignIfExist(additionalItems_, value, "additionalItems");

        // String
        AssignIfExist(minLength_, value, "minLength");
        AssignIfExist(maxLength_, value, "maxLength");

#if RAPIDJSON_SCHEMA_HAS_REGEX
        if (const ValueType* v = GetMember(value, "pattern"))
            pattern_ = CreatePattern(*v);
#endif // RAPIDJSON_SCHEMA_HAS_REGEX

        // Number
        ConstMemberIterator minimumItr = value.FindMember("minimum");
        if (minimumItr != value.MemberEnd())
            if (minimumItr->value.IsNumber())
                minimum_ = minimumItr->value.GetDouble();

        ConstMemberIterator maximumItr = value.FindMember("maximum");
        if (maximumItr != value.MemberEnd())
            if (maximumItr->value.IsNumber())
                maximum_ = maximumItr->value.GetDouble();

        AssignIfExist(exclusiveMinimum_, value, "exclusiveMinimum");
        AssignIfExist(exclusiveMaximum_, value, "exclusiveMaximum");

        ConstMemberIterator multipleOfItr = value.FindMember("multipleOf");
        if (multipleOfItr != value.MemberEnd()) {
            if (multipleOfItr->value.IsNumber()) {
                multipleOf_ = multipleOfItr->value.GetDouble();
                hasMultipleOf_ = true;
            }
        }
    }

    ~BaseSchema() {
        delete not_;
        delete [] properties_;
        delete additionalPropertySchema_;
#if RAPIDJSON_SCHEMA_HAS_REGEX
        delete [] patternProperties_;
#endif
        delete itemsList_;
        for (SizeType i = 0; i < itemsTupleCount_; i++)
            delete itemsTuple_[i];
        delete [] itemsTuple_;
#if RAPIDJSON_SCHEMA_USE_STDREGEX
        delete pattern_;
#endif
    }

    bool BeginValue(Context& context) const {
        if (context.inArray) {
            if (itemsList_)
                context.valueSchema = itemsList_;
            else if (itemsTuple_) {
                if (context.arrayElementIndex < itemsTupleCount_)
                    context.valueSchema = itemsTuple_[context.arrayElementIndex];
                else if (additionalItems_)
                    context.valueSchema = GetTypeless();
                else
                    return false;
            }
            else
                context.valueSchema = GetTypeless();

            context.arrayElementIndex++;
        }
        return true;
    }

    bool EndValue(Context& context) const {
        if (allOf_.schemas)
            for (SizeType i_ = 0; i_ < allOf_.count; i_++)
                if (!context.allOfValidators.validators[i_]->IsValid())
                    return false;
        
        if (anyOf_.schemas) {
            for (SizeType i_ = 0; i_ < anyOf_.count; i_++)
                if (context.anyOfValidators.validators[i_]->IsValid())
                    goto foundAny;
            return false;
            foundAny:;
        }

        if (oneOf_.schemas) {
            bool oneValid = false;
            for (SizeType i_ = 0; i_ < oneOf_.count; i_++)
                if (context.oneOfValidators.validators[i_]->IsValid()) {
                    if (oneValid)
                        return false;
                    else
                        oneValid = true;
                }
            if (!oneValid)
                return false;
        }

        return !not_ || !context.notValidator->IsValid();
    }

    bool Null(Context& context) const { 
        CreateLogicValidators(context);
        return
            (type_ & (1 << kNullSchemaType)) &&
            (!enum_.IsArray() || CheckEnum(GenericValue<Encoding>().Move()));
    }
    
    bool Bool(Context& context, bool b) const { 
        CreateLogicValidators(context);
        return
            (type_ & (1 << kBooleanSchemaType)) &&
            (!enum_.IsArray() || CheckEnum(GenericValue<Encoding>(b).Move()));
    }

    bool Int(Context& context, int i) const {
        CreateLogicValidators(context);
        if ((type_ & ((1 << kIntegerSchemaType) | (1 << kNumberSchemaType))) == 0)
            return false;

        return CheckDouble(i) && (!enum_.IsArray() || CheckEnum(GenericValue<Encoding>(i).Move()));
    }

    bool Uint(Context& context, unsigned u) const {
        CreateLogicValidators(context);
        if ((type_ & ((1 << kIntegerSchemaType) | (1 << kNumberSchemaType))) == 0)
            return false;

        return CheckDouble(u) && (!enum_.IsArray() || CheckEnum(GenericValue<Encoding>(u).Move()));
    }

    bool Int64(Context& context, int64_t i) const {
        CreateLogicValidators(context);
        if ((type_ & ((1 << kIntegerSchemaType) | (1 << kNumberSchemaType))) == 0)
            return false;

        return CheckDouble(i) && (!enum_.IsArray() || CheckEnum(GenericValue<Encoding>(i).Move()));
    }

    bool Uint64(Context& context, uint64_t u) const {
        CreateLogicValidators(context);
        if ((type_ & ((1 << kIntegerSchemaType) | (1 << kNumberSchemaType))) == 0)
            return false;

        return CheckDouble(u) && (!enum_.IsArray() || CheckEnum(GenericValue<Encoding>(u).Move()));
    }

    bool Double(Context& context, double d) const {
        CreateLogicValidators(context);
        if ((type_ & (1 << kNumberSchemaType)) == 0)
            return false;

        return CheckDouble(d) && (!enum_.IsArray() || CheckEnum(GenericValue<Encoding>(d).Move()));
    }
    
    bool String(Context& context, const Ch* str, SizeType length, bool) const {
        (void)str;
        CreateLogicValidators(context);
        if ((type_ & (1 << kStringSchemaType)) == 0)
            return false;

        if (length < minLength_ || length > maxLength_)
            return false;

#if RAPIDJSON_SCHEMA_HAS_REGEX
        if (pattern_ && !IsPatternMatch(*pattern_, str, length))
            return false;
#endif

        return !enum_.IsArray() || CheckEnum(GenericValue<Encoding>(str, length).Move());
    }

    bool StartObject(Context& context) const { 
        CreateLogicValidators(context);
        if ((type_ & (1 << kObjectSchemaType)) == 0)
            return false;

        context.objectRequiredCount = 0;
        if (hasDependencies_) {
            context.objectDependencies = new bool[propertyCount_];
            std::memset(context.objectDependencies, 0, sizeof(bool) * propertyCount_);
        }
        return true; 
    }
    
    bool Key(Context& context, const Ch* str, SizeType len, bool) const {
        CreateLogicValidators(context);
        if ((type_ & (1 << kObjectSchemaType)) == 0)
            return false;
        
        SizeType index;
        if (FindPropertyIndex(str, len, &index)) {
            context.valueSchema = properties_[index].schema;

            if (properties_[index].required)
                context.objectRequiredCount++;

            if (hasDependencies_)
                context.objectDependencies[index] = true;

            return true;
        }

#if RAPIDJSON_SCHEMA_HAS_REGEX
        if (patternProperties_)
            for (SizeType i = 0; i < patternPropertyCount_; i++)
                if (patternProperties_[i].pattern && IsPatternMatch(*patternProperties_[i].pattern, str, len)) {
                    context.valueSchema = patternProperties_[i].schema;
                    return true;
                }
#endif

        if (additionalPropertySchema_) {
            context.valueSchema = additionalPropertySchema_;
            return true;
        }
        else if (additionalProperty_) {
            context.valueSchema = GetTypeless();
            return true;
        }
        else
            return false;
    }

    bool EndObject(Context& context, SizeType memberCount) const {
        CreateLogicValidators(context);
        if ((type_ & (1 << kObjectSchemaType)) == 0)
            return false;
        
        if (context.objectRequiredCount != requiredCount_ || memberCount < minProperties_ || memberCount > maxProperties_)
            return false;

        if (hasDependencies_)
            for (SizeType sourceIndex = 0; sourceIndex < propertyCount_; sourceIndex++)
                if (context.objectDependencies[sourceIndex] && properties_[sourceIndex].dependencies)
                    for (SizeType targetIndex = 0; targetIndex < propertyCount_; targetIndex++)
                        if (properties_[sourceIndex].dependencies[targetIndex] && !context.objectDependencies[targetIndex])
                            return false;

        return true;
    }

    bool StartArray(Context& context) const { 
        CreateLogicValidators(context);
        if ((type_ & (1 << kArraySchemaType)) == 0)
            return false;
        
        context.arrayElementIndex = 0;
        context.inArray = true;
        return true;
    }

    bool EndArray(Context& context, SizeType elementCount) const { 
        CreateLogicValidators(context);
        if ((type_ & (1 << kArraySchemaType)) == 0)
            return false;
        
        context.inArray = false;
        return elementCount >= minItems_ && elementCount <= maxItems_;
    }

private:
    static const BaseSchema<Encoding>* GetTypeless() {
        static BaseSchema<Encoding> typeless(Value(kObjectType).Move());
        return &typeless;
    }

    template <typename ValueType>
    static const ValueType* GetMember(const ValueType& value, const char* name) {
        typename ValueType::ConstMemberIterator itr = value.FindMember(name);
        return itr != value.MemberEnd() ? &(itr->value) : 0;
    }

    template <typename ValueType>
    static void AssignIfExist(bool& out, const ValueType& value, const char* name) {
        if (const ValueType* v = GetMember(value, name))
            if (v->IsBool())
                out = v->GetBool();
    }

    template <typename ValueType>
    static void AssignIfExist(SizeType& out, const ValueType& value, const char* name) {
        if (const ValueType* v = GetMember(value, name))
            if (v->IsUint64() && v->GetUint64() <= SizeType(~0))
                out = static_cast<SizeType>(v->GetUint64());
    }

    template <typename ValueType>
    static void AssigIfExist(BaseSchemaArray<Encoding>& out, const ValueType& value, const char* name) {
        if (const ValueType* v = GetMember(value, name))
            if (v->IsArray() && v->Size() > 0) {
                out.count = v->Size();
                out.schemas = new BaseSchema*[out.count];
                memset(out.schemas, 0, sizeof(BaseSchema*)* out.count);
                for (SizeType i = 0; i < out.count; i++)
                    out.schemas[i] = new BaseSchema<Encoding>((*v)[i]);
            }
    }

#if RAPIDJSON_SCHEMA_USE_STDREGEX
    template <typename ValueType>
    static std::basic_regex<Ch>* CreatePattern(const ValueType& value) {
        if (value.IsString())
            try {
                return new std::basic_regex<Ch>(value.GetString(), std::size_t(value.GetStringLength()), std::regex_constants::ECMAScript);
            }
            catch (const std::regex_error&) {
            }
        return 0;
    }

    static bool IsPatternMatch(const std::basic_regex<Ch>& pattern, const Ch *str, SizeType length) {
        std::match_results<const Ch*> r;
        return std::regex_search(str, str + length, r, pattern);
    }
#endif // RAPIDJSON_SCHEMA_USE_STDREGEX

    void AddType(const Value& type) {
        if      (type == "null"   ) type_ |= 1 << kNullSchemaType;
        else if (type == "boolean") type_ |= 1 << kBooleanSchemaType;
        else if (type == "object" ) type_ |= 1 << kObjectSchemaType;
        else if (type == "array"  ) type_ |= 1 << kArraySchemaType;
        else if (type == "string" ) type_ |= 1 << kStringSchemaType;
        else if (type == "integer") type_ |= 1 << kIntegerSchemaType;
        else if (type == "number" ) type_ |= (1 << kNumberSchemaType) | (1 << kIntegerSchemaType);
    }

    bool CheckEnum(const GenericValue<Encoding>& v) const {
        for (typename GenericValue<Encoding>::ConstValueIterator itr = enum_.Begin(); itr != enum_.End(); ++itr)
            if (v == *itr)
                return true;
        return false;
    }

    void CreateLogicValidators(Context& context) const {
        if (allOf_.schemas) CreateSchemaValidators(context.allOfValidators, allOf_);
        if (anyOf_.schemas) CreateSchemaValidators(context.anyOfValidators, anyOf_);
        if (oneOf_.schemas) CreateSchemaValidators(context.oneOfValidators, oneOf_);
        if (not_ && !context.notValidator)
            context.notValidator = new GenericSchemaValidator<Encoding, BaseReaderHandler<>, CrtAllocator>(*not_);
    }

    void CreateSchemaValidators(SchemaValidatorArray<Encoding>& validators, const BaseSchemaArray<Encoding>& schemas) const {
        if (!validators.validators) {
            validators.validators = new GenericSchemaValidator<Encoding, BaseReaderHandler<>, CrtAllocator>*[schemas.count];
            validators.count = schemas.count;
            for (SizeType i = 0; i < schemas.count; i++)
                validators.validators[i] = new GenericSchemaValidator<Encoding, BaseReaderHandler<>, CrtAllocator>(*schemas.schemas[i]);
        }
    }

    // O(n)
    template <typename ValueType>
    bool FindPropertyIndex(const ValueType& name, SizeType* outIndex) const {
        for (SizeType index = 0; index < propertyCount_; index++)
            if (properties_[index].name == name) {
                *outIndex = index;
                return true;
            }
        return false;
    }

    // O(n)
    bool FindPropertyIndex(const Ch* str, SizeType length, SizeType* outIndex) const {
        for (SizeType index = 0; index < propertyCount_; index++)
            if (properties_[index].name.GetStringLength() == length && std::memcmp(properties_[index].name.GetString(), str, length) == 0) {
                *outIndex = index;
                return true;
            }
        return false;
    }

    bool CheckDouble(double d) const {
        if (exclusiveMinimum_ ? d <= minimum_ : d < minimum_) return false;
        if (exclusiveMaximum_ ? d >= maximum_ : d > maximum_) return false;
        if (hasMultipleOf_ && std::fmod(d, multipleOf_) != 0.0) return false;
        return true;
    }

    struct Property {
        Property() : schema(), dependencies(), required(false) {}
        ~Property() { 
            delete schema;
            delete[] dependencies;
        }

        GenericValue<Encoding> name;
        BaseSchema<Encoding>* schema;
        bool* dependencies;
        bool required;
    };

#if RAPIDJSON_SCHEMA_HAS_REGEX
    struct PatternProperty {
        PatternProperty() : schema(), pattern() {}
        ~PatternProperty() {
            delete schema;
            delete pattern;
        }

        BaseSchema<Encoding>* schema;
#if RAPIDJSON_SCHEMA_USE_STDREGEX
        std::basic_regex<Ch>* pattern;
#endif
    };
#endif

    MemoryPoolAllocator<> allocator_;
    GenericValue<Encoding> enum_;
    BaseSchemaArray<Encoding> allOf_;
    BaseSchemaArray<Encoding> anyOf_;
    BaseSchemaArray<Encoding> oneOf_;
    BaseSchema<Encoding>* not_;
    unsigned type_; // bitmask of kSchemaType

    Property* properties_;
    BaseSchema<Encoding>* additionalPropertySchema_;
#if RAPIDJSON_SCHEMA_HAS_REGEX
    PatternProperty* patternProperties_;
    SizeType patternPropertyCount_;
#endif
    SizeType propertyCount_;
    SizeType requiredCount_;
    SizeType minProperties_;
    SizeType maxProperties_;
    bool additionalProperty_;
    bool hasDependencies_;

    BaseSchema<Encoding>* itemsList_;
    BaseSchema<Encoding>** itemsTuple_;
    SizeType itemsTupleCount_;
    SizeType minItems_;
    SizeType maxItems_;
    bool additionalItems_;

#if RAPIDJSON_SCHEMA_USE_STDREGEX
    std::basic_regex<Ch>* pattern_;
#endif
    SizeType minLength_;
    SizeType maxLength_;

    double minimum_;
    double maximum_;
    double multipleOf_;
    bool hasMultipleOf_;
    bool exclusiveMinimum_;
    bool exclusiveMaximum_;
};

template <typename Encoding, typename Allocator = MemoryPoolAllocator<> >
class GenericSchema {
public:
    template <typename T1, typename T2, typename T3>
    friend class GenericSchemaValidator;

    template <typename DocumentType>
    GenericSchema(const DocumentType& document) : root_() {
        root_ = new BaseSchema<Encoding>(static_cast<const typename DocumentType::ValueType&>(document));
    }

    ~GenericSchema() {
        delete root_;
    }

private:
    BaseSchema<Encoding>* root_;
};

typedef GenericSchema<UTF8<> > Schema;

template <typename Encoding, typename OutputHandler = BaseReaderHandler<Encoding>, typename Allocator = CrtAllocator >
class GenericSchemaValidator {
public:
    typedef typename Encoding::Ch Ch;               //!< Character type derived from Encoding.
    typedef GenericSchema<Encoding> SchemaT;
    friend class BaseSchema<Encoding>;

    GenericSchemaValidator(
        const SchemaT& schema,
        Allocator* allocator = 0, 
        size_t schemaStackCapacity = kDefaultSchemaStackCapacity/*,
        size_t documentStackCapacity = kDefaultDocumentStackCapacity*/)
        :
        root_(*schema.root_), 
        outputHandler_(nullOutputHandler_),
        schemaStack_(allocator, schemaStackCapacity),
        // documentStack_(allocator, documentStackCapacity),
        valid_(true)
    {
    }

    GenericSchemaValidator( 
        const SchemaT& schema,
        OutputHandler& outputHandler,
        Allocator* allocator = 0,
        size_t schemaStackCapacity = kDefaultSchemaStackCapacity/*,
        size_t documentStackCapacity = kDefaultDocumentStackCapacity*/)
        :
        root_(*schema.root_), 
        outputHandler_(outputHandler),
        schemaStack_(allocator, schemaStackCapacity),
        // documentStack_(allocator, documentStackCapacity),
        valid_(true)
    {
    }

    ~GenericSchemaValidator() {
        Reset();
    }

    void Reset() {
        while (!schemaStack_.Empty())
            PopSchema();
        //documentStack_.Clear();
        valid_ = true;
    };

    bool IsValid() { return valid_; }

#define RAPIDJSON_SCHEMA_HANDLE_BEGIN_(method, arg1)\
    if (!valid_) return false; \
    if (!BeginValue() || !CurrentSchema().method arg1) return valid_ = false;

#define RAPIDJSON_SCHEMA_HANDLE_LOGIC_(method, arg2)\
    for (Context* context = schemaStack_.template Bottom<Context>(); context <= schemaStack_.template Top<Context>(); context++) {\
        if (context->allOfValidators.validators)\
            for (SizeType i_ = 0; i_ < context->allOfValidators.count; i_++)\
                context->allOfValidators.validators[i_]->method arg2;\
        if (context->anyOfValidators.validators)\
            for (SizeType i_ = 0; i_ < context->anyOfValidators.count; i_++)\
                context->anyOfValidators.validators[i_]->method arg2;\
        if (context->oneOfValidators.validators)\
            for (SizeType i_ = 0; i_ < context->oneOfValidators.count; i_++)\
                context->oneOfValidators.validators[i_]->method arg2;\
        if (context->notValidator)\
            context->notValidator->method arg2;\
    }

#define RAPIDJSON_SCHEMA_HANDLE_END_(method, arg2)\
    return valid_ = EndValue() && outputHandler_.method arg2

#define RAPIDJSON_SCHEMA_HANDLE_VALUE_(method, arg1, arg2) \
    RAPIDJSON_SCHEMA_HANDLE_BEGIN_(method, arg1);\
    RAPIDJSON_SCHEMA_HANDLE_LOGIC_(method, arg2);\
    RAPIDJSON_SCHEMA_HANDLE_END_  (method, arg2)

    bool Null()             { RAPIDJSON_SCHEMA_HANDLE_VALUE_(Null,   (CurrentContext()   ), ( )); }
    bool Bool(bool b)       { RAPIDJSON_SCHEMA_HANDLE_VALUE_(Bool,   (CurrentContext(), b), (b)); }
    bool Int(int i)         { RAPIDJSON_SCHEMA_HANDLE_VALUE_(Int,    (CurrentContext(), i), (i)); }
    bool Uint(unsigned u)   { RAPIDJSON_SCHEMA_HANDLE_VALUE_(Uint,   (CurrentContext(), u), (u)); }
    bool Int64(int64_t i)   { RAPIDJSON_SCHEMA_HANDLE_VALUE_(Int64,  (CurrentContext(), i), (i)); }
    bool Uint64(uint64_t u) { RAPIDJSON_SCHEMA_HANDLE_VALUE_(Uint64, (CurrentContext(), u), (u)); }
    bool Double(double d)   { RAPIDJSON_SCHEMA_HANDLE_VALUE_(Double, (CurrentContext(), d), (d)); }
    bool String(const Ch* str, SizeType length, bool copy)
                                    { RAPIDJSON_SCHEMA_HANDLE_VALUE_(String, (CurrentContext(), str, length, copy), (str, length, copy)); }

    bool StartObject() {
        RAPIDJSON_SCHEMA_HANDLE_BEGIN_(StartObject, (CurrentContext()));
        RAPIDJSON_SCHEMA_HANDLE_LOGIC_(StartObject, ());
        return valid_ = outputHandler_.StartObject();
    }
    
    bool Key(const Ch* str, SizeType len, bool copy) {
        if (!valid_) return false;
        if (!CurrentSchema().Key(CurrentContext(), str, len, copy)) return valid_ = false;
        RAPIDJSON_SCHEMA_HANDLE_LOGIC_(Key, (str, len, copy));
        return valid_ = outputHandler_.Key(str, len, copy);
    }
    
    bool EndObject(SizeType memberCount) { 
        if (!valid_) return false;
        if (!CurrentSchema().EndObject(CurrentContext(), memberCount)) return valid_ = false;
        RAPIDJSON_SCHEMA_HANDLE_LOGIC_(EndObject, (memberCount));
        RAPIDJSON_SCHEMA_HANDLE_END_  (EndObject, (memberCount));
    }

    bool StartArray() {
        RAPIDJSON_SCHEMA_HANDLE_BEGIN_(StartArray, (CurrentContext()));
        RAPIDJSON_SCHEMA_HANDLE_LOGIC_(StartArray, ());
        return valid_ = outputHandler_.StartArray();
    }
    
    bool EndArray(SizeType elementCount) {
        if (!valid_) return false;
        if (!CurrentSchema().EndArray(CurrentContext(), elementCount)) return valid_ = false;
        RAPIDJSON_SCHEMA_HANDLE_LOGIC_(EndArray, (elementCount));
        RAPIDJSON_SCHEMA_HANDLE_END_  (EndArray, (elementCount));
    }

#undef RAPIDJSON_SCHEMA_HANDLE_BEGIN_
#undef RAPIDJSON_SCHEMA_HANDLE_LOGIC_
#undef RAPIDJSON_SCHEMA_HANDLE_VALUE_

    // Implementation of ISchemaValidatorFactory<Encoding>
    GenericSchemaValidator<Encoding>* CreateSchemaValidator(const BaseSchema<Encoding>& root) {
        return new GenericSchemaValidator(root);
    }

private:
    typedef BaseSchema<Encoding> BaseSchemaType;
    typedef typename BaseSchemaType::Context Context;

    GenericSchemaValidator( 
        const BaseSchemaType& root,
        Allocator* allocator = 0,
        size_t schemaStackCapacity = kDefaultSchemaStackCapacity/*,
        size_t documentStackCapacity = kDefaultDocumentStackCapacity*/)
        :
        root_(root),
        outputHandler_(nullOutputHandler_),
        schemaStack_(allocator, schemaStackCapacity),
        // documentStack_(allocator, documentStackCapacity),
        valid_(true)
    {
    }

    bool BeginValue() {
        if (schemaStack_.Empty())
            PushSchema(root_);
        else {
            if (!CurrentSchema().BeginValue(CurrentContext()))
                return false;

            if (CurrentContext().valueSchema)
                PushSchema(*CurrentContext().valueSchema);
        }
        return true;
    }

    bool EndValue() {
        if (!CurrentSchema().EndValue(CurrentContext()))
            return false;

        PopSchema();
        if (!schemaStack_.Empty() && CurrentContext().multiTypeSchema)
             PopSchema();

        return true;
    }

    void PushSchema(const BaseSchemaType& schema) { *schemaStack_.template Push<Context>() = Context(&schema); }
    void PopSchema() { schemaStack_.template Pop<Context>(1)->~Context(); }
    const BaseSchemaType& CurrentSchema() { return *schemaStack_.template Top<Context>()->schema; }
    Context& CurrentContext() { return *schemaStack_.template Top<Context>(); }

    static const size_t kDefaultSchemaStackCapacity = 256;
    //static const size_t kDefaultDocumentStackCapacity = 256;
    const BaseSchemaType& root_;
    BaseReaderHandler<Encoding> nullOutputHandler_;
    OutputHandler& outputHandler_;
    internal::Stack<Allocator> schemaStack_;    //!< stack to store the current path of schema (BaseSchemaType *)
    //internal::Stack<Allocator> documentStack_;  //!< stack to store the current path of validating document (Value *)
    bool valid_;
};

typedef GenericSchemaValidator<UTF8<> > SchemaValidator;

RAPIDJSON_NAMESPACE_END

#if defined(__GNUC__)
RAPIDJSON_DIAG_POP
#endif

#endif // RAPIDJSON_SCHEMA_H_
