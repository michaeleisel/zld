//===- lib/Core/APIJSONSerializer.cpp - TAPI API JSONSerializer -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Core/APIJSONSerializer.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::json;
using namespace clang;

TAPI_NAMESPACE_INTERNAL_BEGIN

class APIJSONVisitor : public APIVisitor {
public:
  APIJSONVisitor(const APIJSONOption &options) : options(options) {}
  ~APIJSONVisitor() override {}

  friend APIJSONSerializer;

  void visitGlobal(const GlobalRecord &) override;
  void visitEnumConstant(const EnumConstantRecord &) override;
  void visitObjCInterface(const ObjCInterfaceRecord &) override;
  void visitObjCCategory(const ObjCCategoryRecord &) override;
  void visitObjCProtocol(const ObjCProtocolRecord &) override;
  void visitTypeDef(const APIRecord &) override;

private:
  const APIJSONOption &options;
  Array Global;
  Array Interface;
  Array Category;
  Array Protocol;
  Array Enum;
  Array Typedef;
};

class APIJSONParser {
public:
  APIJSONParser(API &api, bool publicOnly = false)
      : result(api), publicOnly(publicOnly) {}
  Error parse(Object *root);

private:
  // top level parsers.
  Error parseGlobals(Array &globals);
  Error parseInterfaces(Array &interfaces);
  Error parseCategories(Array &categories);
  Error parseProtocols(Array &protocols);
  Error parseEnums(Array &enums);
  Error parseTypedefs(Array &types);
  Error parseBinaryInfo(Object &binaryInfo);
  Error parsePotentiallyDefinedSelectors(Array &selectors);

  // helper functions.
  Expected<StringRef> parseString(StringRef key, const Object *obj,
                                  llvm::Twine error);
  bool parseBinaryField(StringRef key, const Object *obj);

  // parse structs.
  Expected<StringRef> parseName(const Object *obj);
  Expected<StringRef> parseSuperClass(const Object *obj);
  Expected<APILoc> parseLocation(const Object *obj);
  Expected<AvailabilityInfo> parseAvailability(const Object *obj);
  Expected<APIAccess> parseAccess(const Object *obj);
  Expected<GVKind> parseGlobalKind(const Object *obj);
  Expected<APILinkage> parseLinkage(const Object *obj);
  APIFlags parseFlags(const Object *obj);

  using PAKind = ObjCPropertyRecord::AttributeKind;
  friend inline APIJSONParser::PAKind operator|=(APIJSONParser::PAKind &a,
                                                 const APIJSONParser::PAKind b);
  Expected<PAKind> parsePropertyAttribute(const Object *obj);
  Expected<ObjCInstanceVariableRecord::AccessControl>
  parseAccessControl(const Object *obj);

  // parse sub objects.
  Error parseConformedProtocols(ObjCContainerRecord *container,
                                const Object *obj);
  Error parseMethods(ObjCContainerRecord *container, const Object *object,
                     bool isInstanceMethod);
  Error parseProperties(ObjCContainerRecord *container, const Object *object);
  Error parseIvars(ObjCContainerRecord *container, const Object *object);

  // references to output.
  API &result;

  bool publicOnly;
};

// Helper to write boolean value. Default is to skip false boolean value.
static void serializeBoolean(Object &obj, StringRef key, bool value) {
  if (!value)
    return;
  obj[key] = true;
}

// Helper to write array. Default is to skip empty array.
static void serializeArray(Object &obj, StringRef key,
                           const std::vector<StringRef> array) {
  if (array.empty())
    return;
  Array values;
  for (const auto &e : array)
    values.emplace_back(e);
  obj[key] = std::move(values);
}

static void serializeLocation(Object &obj, const APILoc &loc) {
  // skip invalid location.
  if (loc.isInvalid())
    return;

  std::string str;
  raw_string_ostream rss(str);
  rss << loc.getFilename() << ":" << loc.getLine() << ":" << loc.getColumn();
  obj["loc"] = rss.str();
}

static void serializeAvailability(Object &obj, const AvailabilityInfo &avail) {
  if (avail.isDefault())
    return;

  std::string str;
  raw_string_ostream rss(str);
  rss << avail;
  obj["availability"] = rss.str();
}

static void serializeLinkage(Object &obj, APILinkage linkage) {
  switch (linkage) {
  case APILinkage::Exported:
    obj["linkage"] = "exported";
    break;
  case APILinkage::Reexported:
    obj["linkage"] = "re-exported";
    break;
  case APILinkage::Internal:
    obj["linkage"] = "internal";
    break;
  case APILinkage::External:
    obj["linkage"] = "external";
    break;
  case APILinkage::Unknown:
    // do nothing;
    break;
  }
}

static void serializeFlags(Object &obj, APIFlags flags) {
  serializeBoolean(obj, "weakDefined",
                   (flags & APIFlags::WeakDefined) == APIFlags::WeakDefined);
  serializeBoolean(obj, "weakReferenced",
                   (flags & APIFlags::WeakReferenced) ==
                       APIFlags::WeakReferenced);
  serializeBoolean(obj, "threadLocalValue",
                   (flags & APIFlags::ThreadLocalValue) ==
                       APIFlags::ThreadLocalValue);
}

static void serializeAPIRecord(Object &obj, const APIRecord &var,
                               const APIJSONOption &options) {
  obj["name"] = var.name;

  serializeLocation(obj, var.loc);
  serializeAvailability(obj, var.availability);
  serializeLinkage(obj, var.linkage);
  serializeFlags(obj, var.flags);

  if (options.publicOnly) {
    assert(var.access == APIAccess::Public && "emit public only");
    return;
  }

  switch (var.access) {
  case APIAccess::Public:
    obj["access"] = "public";
    break;
  case APIAccess::Private:
    obj["access"] = "private";
    break;
  case APIAccess::Project:
    obj["access"] = "project";
    break;
  case APIAccess::Unknown:
    // do nothing;
    break;
  }
}

static void serializeGlobalRecord(Object &obj, const GlobalRecord &var,
                                  const APIJSONOption &options) {
  serializeAPIRecord(obj, var, options);

  switch (var.kind) {
  case GVKind::Function:
    obj["kind"] = "function";
    break;
  case GVKind::Variable:
    obj["kind"] = "variable";
    break;
  case GVKind::Unknown:
    // do nothing;
    break;
  }
}

static void serializeMethod(Array &container, const ObjCMethodRecord *method,
                            const APIJSONOption &options) {
  if (options.publicOnly && method->access != APIAccess::Public)
    return;

  Object obj;
  serializeAPIRecord(obj, *method, options);
  serializeBoolean(obj, "optional", method->isOptional);
  serializeBoolean(obj, "dynamic", method->isDynamic);
  container.emplace_back(std::move(obj));
}

static void serializeProperty(Array &container,
                              const ObjCPropertyRecord *property,
                              const APIJSONOption &options) {
  if (options.publicOnly && property->access != APIAccess::Public)
    return;

  Object obj;
  serializeAPIRecord(obj, *property, options);

  Array attributes;
  if (property->isReadOnly())
    attributes.emplace_back("readonly");
  if (property->isDynamic())
    attributes.emplace_back("dynamic");
  if (property->isClassProperty())
    attributes.emplace_back("class");
  if (!attributes.empty())
    obj["attr"] = std::move(attributes);

  serializeBoolean(obj, "optional", property->isOptional);
  obj["getter"] = property->getterName;

  if (!property->isReadOnly())
    obj["setter"] = property->setterName;

  container.emplace_back(std::move(obj));
}

static void serializeInstanceVariable(Array &container,
                                      const ObjCInstanceVariableRecord *ivar,
                                      const APIJSONOption &options) {
  if (options.publicOnly && ivar->access != APIAccess::Public)
    return;

  Object obj;
  serializeAPIRecord(obj, *ivar, options);

  switch (ivar->accessControl) {
  case ObjCInstanceVariableRecord::AccessControl::Private:
    obj["accessControl"] = "private";
    break;
  case ObjCInstanceVariableRecord::AccessControl::Protected:
    obj["accessControl"] = "protected";
    break;
  case ObjCInstanceVariableRecord::AccessControl::Public:
    obj["accessControl"] = "public";
    break;
  case ObjCInstanceVariableRecord::AccessControl::Package:
    obj["accessControl"] = "package";
    break;
  case ObjCInstanceVariableRecord::AccessControl::None:
    break; // ignore;
  }

  container.emplace_back(std::move(obj));
}

void APIJSONVisitor::visitGlobal(const GlobalRecord &record) {
  if (options.publicOnly && record.access != APIAccess::Public)
    return;
  if (options.externalOnly && !record.isExported())
    return;

  Object root;
  serializeGlobalRecord(root, record, options);
  Global.emplace_back(std::move(root));
}

static void serializeObjCContainer(Object &root,
                                   const ObjCContainerRecord &record,
                                   const APIJSONOption &options) {
  serializeAPIRecord(root, record, options);

  if (!record.protocols.empty()) {
    Array protocols;
    for (const auto protocol : record.protocols)
      if (!protocol.empty())
        protocols.emplace_back(protocol);
    root["protocols"] = std::move(protocols);
  }

  if (!record.methods.empty()) {
    Array instanceMethodRoot, classMethodRoot;
    for (const auto *method : record.methods) {
      if (method->isInstanceMethod)
        serializeMethod(instanceMethodRoot, method, options);
      else
        serializeMethod(classMethodRoot, method, options);
    }
    if (!instanceMethodRoot.empty())
      root["instanceMethods"] = std::move(instanceMethodRoot);
    if (!classMethodRoot.empty())
      root["classMethods"] = std::move(classMethodRoot);
  }

  if (!record.properties.empty()) {
    Array propertyRoot;
    for (const auto *property : record.properties)
      serializeProperty(propertyRoot, property, options);
    root["properties"] = std::move(propertyRoot);
  }

  if (!record.ivars.empty()) {
    Array ivarRoot;
    for (const auto *ivar : record.ivars)
      serializeInstanceVariable(ivarRoot, ivar, options);
    root["ivars"] = std::move(ivarRoot);
  }
}

void APIJSONVisitor::visitObjCInterface(const ObjCInterfaceRecord &interface) {
  if (options.publicOnly && interface.access != APIAccess::Public)
    return;
  if (options.externalOnly && !interface.isExported())
    return;

  Object root;
  serializeObjCContainer(root, interface, options);
  root["super"] = interface.superClassName;
  serializeLinkage(root, interface.linkage);
  serializeBoolean(root, "hasException", interface.hasExceptionAttribute);

  if (!interface.categories.empty()) {
    Array categories;
    for (const auto *category : interface.categories)
      if (!category->name.empty())
        categories.emplace_back(category->name);
    root["categories"] = std::move(categories);
  }

  Interface.emplace_back(std::move(root));
}

void APIJSONVisitor::visitObjCCategory(const ObjCCategoryRecord &category) {
  if (options.publicOnly && category.access != APIAccess::Public)
    return;

  Object root;
  serializeObjCContainer(root, category, options);
  root["interface"] = category.interfaceName;

  Category.emplace_back(std::move(root));
}

void APIJSONVisitor::visitObjCProtocol(const ObjCProtocolRecord &protocol) {
  if (options.publicOnly && protocol.access != APIAccess::Public)
    return;

  Object root;
  serializeObjCContainer(root, protocol, options);

  Protocol.emplace_back(std::move(root));
}

void APIJSONVisitor::visitEnumConstant(const EnumConstantRecord &record) {
  if (options.publicOnly && record.access != APIAccess::Public)
    return;

  Object root;
  serializeAPIRecord(root, record, options);
  Enum.emplace_back(std::move(root));
}

void APIJSONVisitor::visitTypeDef(const APIRecord &record) {
  if (options.publicOnly && record.access != APIAccess::Public)
    return;

  Object root;
  serializeAPIRecord(root, record, options);
  Typedef.emplace_back(std::move(root));
}

static std::string getPackedVersionString(PackedVersion version) {
  std::string str;
  raw_string_ostream vers(str);
  vers << version;
  return vers.str();
}

static void serializeBinaryInfo(Object &root, const BinaryInfo &binaryInfo,
                                bool noUUID) {
  Object info;
  switch (binaryInfo.fileType) {
  case FileType::MachO_DynamicLibrary:
    info["type"] = "dylib";
    break;
  case FileType::MachO_DynamicLibrary_Stub:
    info["type"] = "stub";
    break;
  case FileType::MachO_Bundle:
    info["type"] = "bundle";
    break;
  default:
    // All other file types are invalid.
    info["type"] = "invalid";
    break;
  }
  info["currentVersion"] = getPackedVersionString(binaryInfo.currentVersion);
  info["compatibilityVersion"] =
      getPackedVersionString(binaryInfo.compatibilityVersion);
  info["installName"] = binaryInfo.installName;
  if (!noUUID)
    info["uuid"] = binaryInfo.uuid;
  // Optional fields below.
  if (!binaryInfo.parentUmbrella.empty())
    info["parentUmbrella"] = binaryInfo.parentUmbrella;
  if (binaryInfo.swiftABIVersion)
    info["swiftABI"] = binaryInfo.swiftABIVersion;
  serializeBoolean(info, "twoLevelNamespace", binaryInfo.isTwoLevelNamespace);
  serializeBoolean(info, "appExtensionSafe", binaryInfo.isAppExtensionSafe);
  serializeArray(info, "allowableClients", binaryInfo.allowableClients);
  serializeArray(info, "reexportedLibraries", binaryInfo.reexportedLibraries);
  root["binaryInfo"] = std::move(info);
}

static void serializePotentiallyDefinedSelectors(
    Object &root, const StringSet<> &potentiallyDefinedSelectors) {
  if (potentiallyDefinedSelectors.empty())
    return;

  std::vector<StringRef> selectors;
  for (const auto &s : potentiallyDefinedSelectors)
    selectors.emplace_back(s.first());

  // sort the selectors for reproducibility.
  llvm::sort(selectors);

  Array output;
  for (auto s : selectors)
    output.emplace_back(s);

  root["potentiallyDefinedSelectors"] = std::move(output);
}

Object APIJSONSerializer::getJSONObject() const {
  json::Object root;
  if (!options.noTarget)
    root["target"] = std::move(api.getTarget().str());

  APIJSONVisitor visitor(options);
  api.visit(visitor);

  auto insertNonEmptyArray = [&](StringRef key, Array array) {
    if (array.empty())
      return;
    root[key] = std::move(array);
  };
  insertNonEmptyArray("globals", visitor.Global);
  insertNonEmptyArray("interfaces", visitor.Interface);
  insertNonEmptyArray("categories", visitor.Category);
  insertNonEmptyArray("protocols", visitor.Protocol);
  insertNonEmptyArray("enums", visitor.Enum);
  insertNonEmptyArray("typedefs", visitor.Typedef);
  serializePotentiallyDefinedSelectors(root,
                                       api.getPotentiallyDefinedSelectors());
  if (api.hasBinaryInfo())
    serializeBinaryInfo(root, api.getBinaryInfo(), options.noUUID);
  return root;
}

void APIJSONSerializer::serialize(raw_ostream &os) const {
  auto root = getJSONObject();
  root["api_json_version"] = 1;
  if (options.compact)
    os << formatv("{0}", Value(std::move(root))) << "\n";
  else
    os << formatv("{0:2}", Value(std::move(root))) << "\n";
}

bool APIJSONParser::parseBinaryField(StringRef key, const Object *obj) {
  auto field = obj->getBoolean(key);
  if (!field)
    return false;
  return *field;
}

Expected<StringRef> APIJSONParser::parseString(StringRef key, const Object *obj,
                                               llvm::Twine error) {
  auto str = obj->getString(key);
  if (!str)
    return make_error<APIJSONError>(error);
  return *str;
}

Expected<StringRef> APIJSONParser::parseName(const Object *obj) {
  return parseString("name", obj, "missing name in json api object");
}

Expected<APILoc> APIJSONParser::parseLocation(const Object *obj) {
  auto loc = obj->getString("loc");
  if (!loc)
    return APILoc();

  // parse location string.
  StringRef location, line, col;
  unsigned lineNum, colNum;
  std::tie(location, col) = loc->rsplit(":");
  auto errCol = col.getAsInteger(10, colNum);
  if (errCol)
    return make_error<APIJSONError>("cannot parse col num in location");
  std::tie(location, line) = location.rsplit(":");
  auto errLine = line.getAsInteger(10, lineNum);
  if (errLine)
    return make_error<APIJSONError>("cannot parse line num in location");

  auto filename = result.copyString(location);
  return APILoc(filename, lineNum, colNum);
}

Expected<AvailabilityInfo> APIJSONParser::parseAvailability(const Object *obj) {
  auto avail = obj->getString("availability");
  if (!avail)
    return AvailabilityInfo();

  // parse availability string.
  StringRef iVer, oVer, remain;
  PackedVersion introduced, obsoleted;
  auto success = avail->consume_front("i:");
  if (!success)
    return make_error<APIJSONError>("malformed availability string");
  std::tie(iVer, remain) = avail->split(" ");
  if (!introduced.parse32(iVer))
    return make_error<APIJSONError>("malformed availability string");
  success = remain.consume_front("o:");
  if (!success)
    return make_error<APIJSONError>("malformed availability string");
  std::tie(oVer, remain) = remain.split(" ");
  if (!obsoleted.parse32(oVer))
    return make_error<APIJSONError>("malformed availability string");
  success = remain.consume_front("u:");
  if (!success)
    return make_error<APIJSONError>("malformed availability string");
  bool unavailable;
  auto err = remain.getAsInteger(10, unavailable);
  if (err)
    return make_error<APIJSONError>("malformed availability string");
  return AvailabilityInfo(introduced, obsoleted, unavailable);
}

Expected<APIAccess> APIJSONParser::parseAccess(const Object *obj) {
  auto access = obj->getString("access");
  if (!access)
    return publicOnly ? APIAccess::Public : APIAccess::Unknown;

  if (*access == "public")
    return APIAccess::Public;
  else if (*access == "private")
    return APIAccess::Private;
  else if (*access == "project")
    return APIAccess::Project;

  return make_error<APIJSONError>("Unknown access " + *access);
}

Expected<GVKind> APIJSONParser::parseGlobalKind(const Object *obj) {
  auto kind = obj->getString("kind");
  if (!kind)
    return GVKind::Unknown;

  if (*kind == "function")
    return GVKind::Function;
  else if (*kind == "variable")
    return GVKind::Variable;

  return make_error<APIJSONError>("Unknown GVKind " + *kind);
}

Expected<APILinkage> APIJSONParser::parseLinkage(const Object *obj) {
  auto linkage = obj->getString("linkage");
  if (!linkage)
    return APILinkage::Unknown;

  if (*linkage == "exported")
    return APILinkage::Exported;
  else if (*linkage == "re-exported")
    return APILinkage::Reexported;
  else if (*linkage == "internal")
    return APILinkage::Internal;
  else if (*linkage == "external")
    return APILinkage::External;

  return make_error<APIJSONError>("Unknown Linkage " + *linkage);
}

APIFlags APIJSONParser::parseFlags(const Object *obj) {
  auto isWeakDefined = parseBinaryField("weakDefined", obj);
  auto isWeakReferenced = parseBinaryField("weakReferenced", obj);
  auto isThreadLocal = parseBinaryField("threadLocalValue", obj);

  auto flags = APIFlags::None;
  if (isWeakDefined)
    flags |= APIFlags::WeakDefined;
  if (isWeakReferenced)
    flags |= APIFlags::WeakReferenced;
  if (isThreadLocal)
    flags |= APIFlags::ThreadLocalValue;

  return flags;
}

inline APIJSONParser::PAKind operator|=(APIJSONParser::PAKind &a,
                                        const APIJSONParser::PAKind b) {
  return a = (APIJSONParser::PAKind)(a | b);
}

Expected<APIJSONParser::PAKind>
APIJSONParser::parsePropertyAttribute(const Object *obj) {
  PAKind kind = PAKind::NoAttr;
  auto attrs = obj->getArray("attr");
  if (!attrs)
    return kind;

  for (const auto &attr : *attrs) {
    auto current = attr.getAsString();
    if (!current)
      return make_error<APIJSONError>("attribute should be string");

    if (*current == "readonly")
      kind |= PAKind::ReadOnly;
    else if (*current == "dynamic")
      kind |= PAKind::Dynamic;
    else if (*current == "class")
      kind |= PAKind::Class;
    else
      return make_error<APIJSONError>("Unknown property attr " + *current);
  }
  return kind;
}

Expected<ObjCInstanceVariableRecord::AccessControl>
APIJSONParser::parseAccessControl(const Object *obj) {
  auto access = obj->getString("access");
  if (!access)
    return ObjCInstanceVariableRecord::AccessControl::None;

  if (*access == "private")
    return ObjCInstanceVariableRecord::AccessControl::Private;
  else if (*access == "protected")
    return ObjCInstanceVariableRecord::AccessControl::Protected;
  else if (*access == "public")
    return ObjCInstanceVariableRecord::AccessControl::Protected;
  else if (*access == "package")
    return ObjCInstanceVariableRecord::AccessControl::Protected;

  return make_error<APIJSONError>("Unknown access control " + *access);
}

Error APIJSONParser::parseGlobals(Array &globals) {
  for (const auto &g : globals) {
    auto *object = g.getAsObject();
    if (!object)
      return make_error<APIJSONError>("Expect to be JSON Object");

    auto name = parseName(object);
    if (!name)
      return name.takeError();
    auto access = parseAccess(object);
    if (!access)
      return access.takeError();
    auto kind = parseGlobalKind(object);
    if (!kind)
      return kind.takeError();
    auto linkage = parseLinkage(object);
    if (!linkage)
      return linkage.takeError();
    auto loc = parseLocation(object);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(object);
    if (!avail)
      return avail.takeError();
    auto flags = parseFlags(object);

    result.addGlobal(*name, flags, *loc, *avail, *access, /*Decl*/ nullptr,
                     *kind, *linkage);
  }

  return Error::success();
}

Expected<StringRef> APIJSONParser::parseSuperClass(const Object *obj) {
  return parseString("super", obj, "missing superclass in json api object");
}

Error APIJSONParser::parseConformedProtocols(ObjCContainerRecord *container,
                                             const Object *obj) {
  auto protocols = obj->getArray("protocols");
  if (!protocols)
    return Error::success();

  for (const auto &p : *protocols) {
    auto protocolStr = p.getAsString();
    if (!protocolStr)
      return make_error<APIJSONError>("conformed protocol should be a string");
    auto protocolName = result.copyString(*protocolStr);
    container->protocols.emplace_back(protocolName);
  }

  return Error::success();
}

Error APIJSONParser::parseMethods(ObjCContainerRecord *container,
                                  const Object *object, bool isInstanceMethod) {
  auto key = isInstanceMethod ? "instanceMethods" : "classMethods";
  auto methods = object->getArray(key);
  if (!methods)
    return Error::success();

  for (const auto &m : *methods) {
    auto method = m.getAsObject();
    if (!method)
      return make_error<APIJSONError>("method should be an object");
    auto name = parseName(method);
    if (!name)
      return name.takeError();
    auto access = parseAccess(method);
    if (!access)
      return access.takeError();
    auto loc = parseLocation(method);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(method);
    if (!avail)
      return avail.takeError();
    auto isOptional = parseBinaryField("optional", method);
    auto isDynamic = parseBinaryField("dynamic", method);
    result.addObjCMethod(container, *name, *loc, *avail, *access,
                         isInstanceMethod, isOptional, isDynamic,
                         /*Decl*/ nullptr);
  }

  return Error::success();
}

Error APIJSONParser::parseProperties(ObjCContainerRecord *container,
                                     const Object *object) {
  auto properties = object->getArray("properties");
  if (!properties)
    return Error::success();

  for (const auto &p : *properties) {
    auto property = p.getAsObject();
    if (!property)
      return make_error<APIJSONError>("property should be an object");
    auto name = parseName(property);
    if (!name)
      return name.takeError();
    auto access = parseAccess(property);
    if (!access)
      return access.takeError();
    auto loc = parseLocation(property);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(property);
    if (!avail)
      return avail.takeError();
    auto attr = parsePropertyAttribute(property);
    if (!attr)
      return attr.takeError();
    auto getter = parseString("getter", property, "cannot find getter");
    if (!getter)
      return getter.takeError();

    StringRef setter;
    if (!(*attr & PAKind::ReadOnly)) {
      auto setterStr = parseString("setter", property, "cannot find setter");
      if (!setterStr)
        return setterStr.takeError();
      setter = *setterStr;
    }
    auto isOptional = parseBinaryField("optional", property);

    result.addObjCProperty(container, *name, *getter, setter, *loc, *avail,
                           *access, *attr, isOptional,
                           /*Decl*/ nullptr);
  }

  return Error::success();
}

Error APIJSONParser::parseIvars(ObjCContainerRecord *container,
                                const Object *object) {
  auto ivars = object->getArray("ivars");
  if (!ivars)
    return Error::success();

  for (const auto &i : *ivars) {
    auto ivar = i.getAsObject();
    if (!ivar)
      return make_error<APIJSONError>("ivar should be an object");
    auto name = parseName(ivar);
    if (!name)
      return name.takeError();
    auto access = parseAccess(ivar);
    if (!access)
      return access.takeError();
    auto loc = parseLocation(ivar);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(ivar);
    if (!avail)
      return avail.takeError();
    auto control = parseAccessControl(ivar);
    if (!control)
      return control.takeError();
    auto linkage = parseLinkage(ivar);
    if (!linkage)
      return linkage.takeError();

    result.addObjCInstanceVariable(container, *name, *loc, *avail, *access,
                                   *control, *linkage, /*Decl*/ nullptr);
  }
  return Error::success();
}

Error APIJSONParser::parseInterfaces(Array &interfaces) {
  for (const auto &interface : interfaces) {
    auto *object = interface.getAsObject();
    if (!object)
      return make_error<APIJSONError>("Expect to be JSON Object");

    auto name = parseName(object);
    if (!name)
      return name.takeError();
    auto access = parseAccess(object);
    if (!access)
      return access.takeError();
    auto loc = parseLocation(object);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(object);
    if (!avail)
      return avail.takeError();
    auto super = parseSuperClass(object);
    if (!super)
      return super.takeError();
    auto linkage = parseLinkage(object);
    if (!linkage)
      return linkage.takeError();
    auto objcClass =
        result.addObjCInterface(*name, *loc, *avail, *access, *linkage, *super,
                                /*Decl*/ nullptr);
    auto exception = parseBinaryField("hasException", object);
    objcClass->hasExceptionAttribute = exception;

    // Don't need to handle categories here.
    auto err = parseConformedProtocols(objcClass, object);
    if (err)
      return err;

    err = parseMethods(objcClass, object, true);
    if (err)
      return err;

    err = parseMethods(objcClass, object, false);
    if (err)
      return err;

    err = parseProperties(objcClass, object);
    if (err)
      return err;

    err = parseIvars(objcClass, object);
    if (err)
      return err;
  }
  return Error::success();
}

Error APIJSONParser::parseCategories(Array &categories) {
  for (const auto &category : categories) {
    auto *object = category.getAsObject();
    if (!object)
      return make_error<APIJSONError>("Expect to be JSON Object");

    auto name = parseName(object);
    if (!name)
      return name.takeError();
    auto access = parseAccess(object);
    if (!access)
      return access.takeError();
    auto loc = parseLocation(object);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(object);
    if (!avail)
      return avail.takeError();
    auto interface = parseString("interface", object, "missing interfaceName");
    if (!interface)
      return interface.takeError();

    auto objcCategory = result.addObjCCategory(*interface, *name, *loc, *avail,
                                               *access, /*Decl*/ nullptr);

    // Don't need to handle categories here.
    auto err = parseConformedProtocols(objcCategory, object);
    if (err)
      return err;

    err = parseMethods(objcCategory, object, true);
    if (err)
      return err;

    err = parseMethods(objcCategory, object, false);
    if (err)
      return err;

    err = parseProperties(objcCategory, object);
    if (err)
      return err;

    err = parseIvars(objcCategory, object);
    if (err)
      return err;
  }
  return Error::success();
}

Error APIJSONParser::parseProtocols(Array &protocols) {
  for (const auto &protocol : protocols) {
    auto *object = protocol.getAsObject();
    if (!object)
      return make_error<APIJSONError>("Expect to be JSON Object");

    auto name = parseName(object);
    if (!name)
      return name.takeError();
    auto access = parseAccess(object);
    if (!access)
      return access.takeError();
    auto loc = parseLocation(object);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(object);
    if (!avail)
      return avail.takeError();
    auto objcProtocol =
        result.addObjCProtocol(*name, *loc, *avail, *access, /*Decl*/ nullptr);

    // Don't need to handle categories here.
    auto err = parseConformedProtocols(objcProtocol, object);
    if (err)
      return err;

    err = parseMethods(objcProtocol, object, true);
    if (err)
      return err;

    err = parseMethods(objcProtocol, object, false);
    if (err)
      return err;

    err = parseProperties(objcProtocol, object);
    if (err)
      return err;

    err = parseIvars(objcProtocol, object);
    if (err)
      return err;
  }
  return Error::success();
}

Error APIJSONParser::parseEnums(Array &enums) {
  for (const auto &e : enums) {
    auto *object = e.getAsObject();
    if (!object)
      return make_error<APIJSONError>("Expect to be JSON Object");

    auto name = parseName(object);
    if (!name)
      return name.takeError();
    auto access = parseAccess(object);
    if (!access)
      return access.takeError();
    auto loc = parseLocation(object);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(object);
    if (!avail)
      return avail.takeError();

    result.addEnumConstant(*name, *loc, *avail, *access,
                           /*Decl*/ nullptr);
  }
  return Error::success();
}

Error APIJSONParser::parseTypedefs(Array &typedefs) {
  for (const auto &type : typedefs) {
    auto *object = type.getAsObject();
    if (!object)
      return make_error<APIJSONError>("Expect to be JSON Object");

    auto name = parseName(object);
    if (!name)
      return name.takeError();
    auto access = parseAccess(object);
    if (!access)
      return access.takeError();
    auto loc = parseLocation(object);
    if (!loc)
      return loc.takeError();
    auto avail = parseAvailability(object);
    if (!avail)
      return avail.takeError();

    result.addTypeDef(*name, *loc, *avail, *access,
                      /*Decl*/ nullptr);
  }
  return Error::success();
}

Error APIJSONParser::parseBinaryInfo(Object &binaryInfo) {
  auto &info = result.getBinaryInfo();
  auto fileType = binaryInfo.getString("type");
  if (!fileType)
    return make_error<APIJSONError>("expected filetype");
  if (*fileType == "dylib")
    info.fileType = FileType::MachO_DynamicLibrary;
  else if (*fileType == "stub")
    info.fileType = FileType::MachO_DynamicLibrary_Stub;
  else if (*fileType == "bundle")
    info.fileType = FileType::MachO_Bundle;
  else if (*fileType == "invalid")
    info.fileType = FileType::Invalid;
  else
    return make_error<APIJSONError>("un-expected filetype: " + *fileType);

  auto currentVersion = binaryInfo.getString("currentVersion");
  if (!currentVersion)
    return make_error<APIJSONError>("expected currentVersion");
  info.currentVersion.parse32(*currentVersion);
  auto compatibilityVersion = binaryInfo.getString("compatibilityVersion");
  if (!compatibilityVersion)
    return make_error<APIJSONError>("expected compatibilityVersion");
  info.compatibilityVersion.parse32(*compatibilityVersion);

  auto installName = binaryInfo.getString("installName");
  if (!installName)
    return make_error<APIJSONError>("missing install name");
  info.installName = result.copyString(*installName);
  auto uuid = binaryInfo.getString("uuid");
  if (uuid)
    info.uuid = result.copyString(*uuid);

  if (auto parentUmbrella = binaryInfo.getString("parentUmbrella"))
    info.parentUmbrella = result.copyString(*parentUmbrella);
  info.isTwoLevelNamespace = parseBinaryField("twoLevelNamespace", &binaryInfo);
  info.isAppExtensionSafe = parseBinaryField("appExtensionSafe", &binaryInfo);

  if (auto allowableClients = binaryInfo.getArray("allowableClients")) {
    for (const auto &client : *allowableClients) {
      auto name = client.getAsString();
      if (!name)
        return make_error<APIJSONError>("allowableClient is not string");
      info.allowableClients.emplace_back(result.copyString(*name));
    }
  }
  if (auto reexportedLibraries = binaryInfo.getArray("reexportedLibraries")) {
    for (const auto &lib : *reexportedLibraries) {
      auto name = lib.getAsString();
      if (!name)
        return make_error<APIJSONError>("reexportedLibrary is not string");
      info.reexportedLibraries.emplace_back(result.copyString(*name));
    }
  }
  return Error::success();
}

Error APIJSONParser::parsePotentiallyDefinedSelectors(Array &selectors) {
  for (auto &s : selectors) {
    auto name = s.getAsString();
    if (!name)
      return make_error<APIJSONError>(
          "potentially defined selector is not string");
    result.getPotentiallyDefinedSelectors().insert(*name);
  }
  return Error::success();
}

Error APIJSONParser::parse(Object *root) {
  auto globals = root->getArray("globals");
  if (globals) {
    auto err = parseGlobals(*globals);
    if (err)
      return err;
  }

  auto protocols = root->getArray("protocols");
  if (protocols) {
    auto err = parseProtocols(*protocols);
    if (err)
      return err;
  }

  auto interfaces = root->getArray("interfaces");
  if (interfaces) {
    auto err = parseInterfaces(*interfaces);
    if (err)
      return err;
  }

  auto categories = root->getArray("categories");
  if (categories) {
    auto err = parseCategories(*categories);
    if (err)
      return err;
  }

  auto enums = root->getArray("enums");
  if (enums) {
    auto err = parseEnums(*enums);
    if (err)
      return err;
  }

  auto typedefs = root->getArray("typedefs");
  if (typedefs) {
    auto err = parseTypedefs(*typedefs);
    if (err)
      return err;
  }

  auto selectors = root->getArray("potentiallyDefinedSelectors");
  if (selectors) {
    auto err = parsePotentiallyDefinedSelectors(*selectors);
    if (err)
      return err;
  }

  auto binaryInfo = root->getObject("binaryInfo");
  if (binaryInfo) {
    auto err = parseBinaryInfo(*binaryInfo);
    if (err)
      return err;
  }

  return Error::success();
}

Expected<API> APIJSONSerializer::parse(StringRef JSON) {
  auto inputValue = json::parse(JSON);
  if (!inputValue)
    return inputValue.takeError();

  auto *root = inputValue->getAsObject();
  if (!root)
    return make_error<APIJSONError>("API is not a JSON Object");

  auto version = root->getInteger("api_json_version");
  if (!version || *version != 1)
    return make_error<APIJSONError>("Input JSON has unsupported version");

  return parse(root);
}

Expected<API> APIJSONSerializer::parse(Object *root, bool publicOnly,
                                       Triple *triple) {
  auto target = root->getString("target");
  std::string targetStr;
  if (target)
    targetStr = target->str();
  else if (triple)
    targetStr = triple->normalize();
  else
    return make_error<APIJSONError>("Input triple is not expected");

  Triple targetTriples(targetStr);
  API result(targetTriples);

  APIJSONParser parser(result, publicOnly);
  auto err = parser.parse(root);
  if (err)
    return std::move(err);

  return result;
}

TAPI_NAMESPACE_INTERNAL_END
