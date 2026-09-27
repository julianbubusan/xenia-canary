/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xur_file.h"

#include <cstring>
#include <map>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/logging.h"

namespace xe {
namespace kernel {
namespace xam {

namespace {

constexpr uint32_t kXurMagic = 0x58554942;  // "XUIB"

struct PropertyDef {
  std::string name;
  XurPropertyType type;
};

struct ClassDef {
  std::string base_class;
  std::vector<PropertyDef> own_properties;
};

// Minimal, extensible class schema, verified against nuihud.xur (a real
// console-extracted file) - add more classes here as new real files are
// inspected. An unknown class name fails the parse loudly rather than
// guessing at its property list. Property order and types come from
// SGCSam/XUIHelper's community-maintained V8 XML schemas
// (Assets/Extensions/V8/XuiElements.xml); only property COUNT and ORDER
// were verified byte-for-byte against real data, types are carried over
// from that same source and not independently verified per-property yet.
const std::map<std::string, ClassDef>& ClassSchema() {
  using T = XurPropertyType;
  static const std::map<std::string, ClassDef> kSchema = {
      {"XuiElement",
       {"",
        {
            {"Id", T::kString},
            {"Width", T::kFloat},
            {"Height", T::kFloat},
            {"Position", T::kVector},
            {"Scale", T::kVector},
            {"Rotation", T::kQuaternion},
            {"Opacity", T::kFloat},
            {"Anchor", T::kUnsigned},
            {"Pivot", T::kVector},
            {"Show", T::kBool},
            {"BlendMode", T::kUnsigned},
            {"DisableTimelineRecursion", T::kBool},
            {"DesignTime", T::kBool},
            {"ColorWriteFlags", T::kUnsigned},
            {"ClipChildren", T::kBool},
            {"EnableEffects", T::kBool},
            {"DisableFocusRecursion", T::kBool},
            {"GripTarget", T::kBool},
            {"Hittable", T::kBool},
            {"LayoutLineBreak", T::kBool},
            {"LayoutFloat", T::kBool},
            {"Column", T::kUnsigned},
            {"Row", T::kUnsigned},
            {"ColumnSpan", T::kUnsigned},
            {"RowSpan", T::kUnsigned},
            {"ColorFactor", T::kColour},
            {"CenterPivot", T::kBool},
        }}},
      {"XuiControl",
       {"XuiElement",
        {
            {"ClassOverride", T::kString},
            {"Visual", T::kString},
            {"Enabled", T::kBool},
            {"UnfocusedInput", T::kBool},
            {"NavLeft", T::kString},
            {"NavRight", T::kString},
            {"NavUp", T::kString},
            {"NavDown", T::kString},
            {"NavTabForward", T::kString},
            {"NavTabBackward", T::kString},
            {"Text", T::kString},
            {"PointSize", T::kFloat},
            {"ImagePath", T::kString},
            {"HasContextMenu", T::kBool},
            {"SizeToText", T::kBool},
            {"UseNuiAsMouse", T::kBool},
            {"AutoId", T::kString},
            {"HoverSelectTimer", T::kFloat},
            {"QuickInput", T::kBool},
        }}},
      {"XuiCanvas", {"XuiElement", {}}},
      {"XuiScene",
       {"XuiControl",
        {
            {"DefaultFocus", T::kString},
            {"TransFrom", T::kString},
            {"TransTo", T::kString},
            {"TransBackFrom", T::kString},
            {"TransBackTo", T::kString},
            {"InterruptTransitions", T::kUnsigned},
            {"IgnorePresses", T::kBool},
            {"RecurseTransitions", T::kBool},
        }}},
      {"ControlPackSystemGesture", {"XuiControl", {}}},
      {"XuiFigure",
       {"XuiElement",
        {
            {"Stroke", T::kObject},
            {"Fill", T::kObject},
            {"Closed", T::kBool},
            {"Points", T::kCustom},
        }}},
      {"XuiGroup", {"XuiElement", {}}},
      {"XuiText",
       {"XuiElement",
        {
            {"Text", T::kString},
            {"TextColor", T::kColour},
            {"DropShadowColor", T::kColour},
            {"PointSize", T::kFloat},
            {"Font", T::kString},
            {"TextStyle", T::kUnsigned},
            {"LineSpacingAdjust", T::kInteger},
            {"TextScale", T::kFloat},
        }}},
      {"XuiImage",
       {"XuiElement",
        {
            {"SizeMode", T::kUnsigned},
            {"ImagePath", T::kString},
            {"BrushFlags", T::kUnsigned},
            {"TextureSurfaceElement", T::kString},
            {"LoadType", T::kUnsigned},
        }}},
      {"XuiButton",
       {"XuiControl",
        {
            {"PressKey", T::kUnsigned},
            {"PressAnimObject", T::kString},
            {"PressAnimStartFrame", T::kString},
            {"PressAnimEndFrame", T::kString},
            {"FocusAnimObject", T::kString},
            {"FocusAnimStartFrame", T::kString},
            {"FocusAnimEndFrame", T::kString},
        }}},
      // XuiSound's own BaseClassName came back self-referential from the
      // fetched schema (almost certainly a summarization error, not a real
      // self-inheriting class) - treated as a root class here, unverified.
      {"XuiSound",
       {"",
        {
            {"State", T::kUnsigned},
            {"Loop", T::kBool},
            {"Finish", T::kBool},
            {"Volume", T::kFloat},
        }}},
      {"XuiSoundXAudio",
       {"XuiSound",
        {
            {"File", T::kString},
        }}},
      {"HUDScene",
       {"XuiScene",
        {
            {"OpenType", T::kUnsigned},
            {"LegendA", T::kString},
            {"LegendB", T::kString},
            {"LegendX", T::kString},
            {"LegendY", T::kString},
            {"ShowGamerInfo", T::kBool},
        }}},
      // HUDBkgndScene/HUDRootScene aren't in XUIHelper's community schema
      // database at all (checked via repo-wide code search) - they're
      // hud.xex-specific and apparently were never captured by the
      // community's extraction tooling. Guessing "extends HUDScene, no own
      // properties" as the most likely hypothesis given the naming
      // ("HUD Background/Root Scene" as a plain specialization) - this
      // will be confirmed or rejected by the propertiesCount validation
      // that already runs for every object, same empirical approach used
      // throughout this file's reverse engineering. If it's wrong, the
      // parse will fail loudly with a count mismatch rather than silently
      // misreading the rest of the file.
      {"HUDBkgndScene", {"HUDScene", {}}},
      {"HUDRootScene", {"HUDScene", {}}},
  };
  return kSchema;
}

bool BuildHierarchy(const std::string& class_name,
                    std::vector<const ClassDef*>* out_chain) {
  std::vector<const ClassDef*> reversed;
  std::string current = class_name;
  const auto& schema = ClassSchema();
  while (!current.empty()) {
    auto it = schema.find(current);
    if (it == schema.end()) {
      XELOGE("XurFile: unknown class '{}' in hierarchy of '{}'", current,
             class_name);
      return false;
    }
    reversed.push_back(&it->second);
    current = it->second.base_class;
  }
  out_chain->assign(reversed.rbegin(), reversed.rend());
  return true;
}

}  // namespace

class XurFile::Cursor {
 public:
  Cursor(const std::vector<uint8_t>& data, size_t offset)
      : data_(data), offset_(offset) {}

  bool ok() const { return ok_; }
  size_t offset() const { return offset_; }

  uint8_t ReadU8() {
    if (offset_ >= data_.size()) {
      ok_ = false;
      return 0;
    }
    return data_[offset_++];
  }

  // Big-endian packed uint: <0xF0 -> 1 byte; 0xF0-0xFE -> 2 bytes
  // ((first&0x0F)<<8 | second); 0xFF -> 5 bytes (1 marker + 4-byte BE
  // uint32). Verified against SGCSam/XUIHelper's ReadPackedUInt.
  uint32_t ReadPackedUInt() {
    uint8_t first = ReadU8();
    if (!ok_) return 0;
    if (first < 0xF0) {
      return first;
    } else if (first != 0xFF) {
      uint8_t second = ReadU8();
      if (!ok_) return 0;
      return ((uint32_t(first) << 8) & 0xF00) | second;
    } else {
      uint32_t v = 0;
      for (int i = 0; i < 4; ++i) {
        v = (v << 8) | ReadU8();
      }
      return v;
    }
  }

  std::string ReadCString() {
    std::string result;
    while (offset_ < data_.size() && data_[offset_] != 0) {
      result.push_back(static_cast<char>(data_[offset_++]));
    }
    if (offset_ >= data_.size()) {
      ok_ = false;
      return "";
    }
    ++offset_;  // Skip nul terminator.
    return result;
  }

 private:
  const std::vector<uint8_t>& data_;
  size_t offset_;
  bool ok_ = true;
};

XurResolvedProperty XurFile::ResolveProperty(const std::string& name,
                                             XurPropertyType type,
                                             uint32_t raw_value) const {
  XurResolvedProperty prop;
  prop.name = name;
  prop.raw_value = raw_value;
  switch (type) {
    case XurPropertyType::kString:
      if (raw_value == 0 || raw_value > strings_.size()) {
        prop.display_value = fmt::format("<bad string index {}>", raw_value);
      } else {
        prop.display_value = fmt::format("\"{}\"", strings_[raw_value - 1]);
      }
      break;
    case XurPropertyType::kFloat:
      if (raw_value >= floats_.size()) {
        prop.display_value = fmt::format("<bad float index {}>", raw_value);
      } else {
        prop.x = floats_[raw_value];
        prop.display_value = fmt::format("{}", prop.x);
      }
      break;
    case XurPropertyType::kVector:
      if (raw_value >= vectors_.size()) {
        prop.display_value = fmt::format("<bad vector index {}>", raw_value);
      } else {
        prop.x = vectors_[raw_value][0];
        prop.y = vectors_[raw_value][1];
        prop.z = vectors_[raw_value][2];
        prop.display_value = fmt::format("({}, {}, {})", prop.x, prop.y, prop.z);
      }
      break;
    case XurPropertyType::kQuaternion:
      if (raw_value >= quaternions_.size()) {
        prop.display_value =
            fmt::format("<bad quaternion index {}>", raw_value);
      } else {
        prop.display_value = fmt::format(
            "({}, {}, {}, {})", quaternions_[raw_value][0],
            quaternions_[raw_value][1], quaternions_[raw_value][2],
            quaternions_[raw_value][3]);
      }
      break;
    case XurPropertyType::kColour:
      if (raw_value >= colours_.size()) {
        prop.display_value = fmt::format("<bad colour index {}>", raw_value);
      } else {
        prop.display_value = fmt::format("#{:08X}", colours_[raw_value]);
      }
      break;
    case XurPropertyType::kBool:
      prop.display_value = raw_value ? "true" : "false";
      break;
    case XurPropertyType::kInteger:
      prop.display_value = fmt::format("{}", static_cast<int32_t>(raw_value));
      break;
    case XurPropertyType::kUnsigned:
      prop.display_value = fmt::format("{}", raw_value);
      break;
    case XurPropertyType::kObject:
      prop.display_value =
          fmt::format("<object index {}, not resolved>", raw_value);
      break;
    case XurPropertyType::kCustom:
      prop.display_value =
          fmt::format("<custom offset {}, not resolved>", raw_value);
      break;
    default:
      prop.display_value = fmt::format("<unresolved: {}>", raw_value);
      break;
  }
  return prop;
}

std::unique_ptr<XurObject> XurFile::ReadObject(Cursor* cursor, int depth) {
  if (depth > 64) {
    XELOGE("XurFile: object nesting too deep, aborting (corrupt data?)");
    return nullptr;
  }

  uint32_t class_index = cursor->ReadPackedUInt();
  // 1-based index into the string table, verified against nuihud.xur.
  if (!cursor->ok() || class_index == 0 || class_index > strings_.size()) {
    XELOGE("XurFile: bad class index {} (strings.size()={})", class_index,
           strings_.size());
    return nullptr;
  }
  auto object = std::make_unique<XurObject>();
  object->class_name = strings_[class_index - 1];

  uint8_t flags = cursor->ReadU8();
  if (!cursor->ok()) return nullptr;
  bool has_properties = (flags & 0x1) != 0;
  bool has_children = (flags & 0x2) != 0;
  bool has_timeline = (flags & 0x4) != 0;
  bool has_shared_properties = (flags & 0x8) != 0;

  if (has_shared_properties) {
    // Not yet encountered in a real file - index into a previously-read
    // property list. Fail loudly rather than silently mis-parsing.
    XELOGE("XurFile: shared-properties flag not yet implemented");
    return nullptr;
  }

  if (has_properties) {
    std::vector<const ClassDef*> hierarchy;
    if (!BuildHierarchy(object->class_name, &hierarchy)) {
      return nullptr;
    }
    uint32_t properties_count = cursor->ReadPackedUInt();
    if (!cursor->ok()) return nullptr;
    uint32_t properties_read = 0;
    for (auto* level : hierarchy) {
      uint32_t mask = cursor->ReadPackedUInt();
      if (!cursor->ok()) return nullptr;
      for (size_t bit = 0; bit < level->own_properties.size(); ++bit) {
        if (mask & (1u << bit)) {
          uint32_t value = cursor->ReadPackedUInt();
          if (!cursor->ok()) return nullptr;
          const auto& def = level->own_properties[bit];
          object->properties.push_back(
              ResolveProperty(def.name, def.type, value));
          ++properties_read;
        }
      }
    }
    if (properties_read != properties_count) {
      XELOGE(
          "XurFile: properties count mismatch for '{}' - expected {}, read "
          "{}",
          object->class_name, properties_count, properties_read);
      return nullptr;
    }
  }

  if (has_children) {
    uint32_t child_count = cursor->ReadPackedUInt();
    if (!cursor->ok()) return nullptr;
    for (uint32_t i = 0; i < child_count; ++i) {
      auto child = ReadObject(cursor, depth + 1);
      if (!child) return nullptr;
      object->children.push_back(std::move(child));
    }
  }

  if (has_timeline) {
    uint32_t named_frames_count = cursor->ReadPackedUInt();
    if (!cursor->ok()) return nullptr;
    if (named_frames_count > 0) {
      // Not yet encountered in a real file - named frame base index plus
      // per-frame objects would follow here.
      XELOGE("XurFile: named timeline frames not yet implemented ({})",
             named_frames_count);
      return nullptr;
    }
    if (has_children) {
      uint32_t timelines_count = cursor->ReadPackedUInt();
      if (!cursor->ok()) return nullptr;
      if (timelines_count > 0) {
        XELOGE("XurFile: timeline objects not yet implemented ({})",
               timelines_count);
        return nullptr;
      }
    }
  }

  return object;
}

std::unique_ptr<XurFile> XurFile::Parse(const std::vector<uint8_t>& data) {
  if (data.size() < 20) {
    XELOGE("XurFile: file too small for header ({} bytes)", data.size());
    return nullptr;
  }
  auto read_be32 = [&](size_t off) {
    return (uint32_t(data[off]) << 24) | (uint32_t(data[off + 1]) << 16) |
           (uint32_t(data[off + 2]) << 8) | uint32_t(data[off + 3]);
  };
  uint32_t magic = read_be32(0);
  uint32_t version = read_be32(4);
  if (magic != kXurMagic) {
    XELOGE("XurFile: bad magic {:08X}", magic);
    return nullptr;
  }
  if (version != 8) {
    XELOGE("XurFile: unsupported version {} (only XUR8 implemented)",
           version);
    return nullptr;
  }
  uint32_t file_size = read_be32(14);
  uint16_t sections_count = (uint16_t(data[18]) << 8) | uint16_t(data[19]);
  if (file_size != data.size()) {
    XELOGW("XurFile: header file_size {} != actual size {}", file_size,
           data.size());
  }

  // 12 packed-uint fields between the 20-byte header and the section table
  // (TotalObjectsCount, TotalUnsharedObjectPropertiesCount,
  // SharedPropertiesArrayCount, SharedCompoundPropertiesCount,
  // SharedCompoundPropertiesArrayCount, TotalKeyframePropertyClassDepth,
  // TotalTimelinePropertyClassDepth, TimelinesCount,
  // KeyframePropertiesCount, KeyframeDataCount, NamedFramesCount,
  // ObjectsWithChildrenCount - per SGCSam/XUIHelper's XUR8CountHeader).
  // Each is variable-length, so this block's total size varies per file -
  // nuihud.xur's small counts happened to all fit in 1 byte each (12 bytes
  // total), which is what caused this to initially look like a fixed-size
  // block before a second real file (hudbkgnd.xur, larger scene, 14 bytes
  // here) exposed the mistake. None of these counts are used for
  // structural parsing yet (the tree walk is self-describing via each
  // object's own flags/counts), so they're read and discarded here only
  // to correctly advance past this block.
  Cursor count_header_cursor(data, 20);
  for (int i = 0; i < 12; ++i) {
    count_header_cursor.ReadPackedUInt();
    if (!count_header_cursor.ok()) {
      XELOGE("XurFile: truncated count header (field {})", i);
      return nullptr;
    }
  }
  size_t section_table_offset = count_header_cursor.offset();
  struct RawSection {
    std::string magic;
    uint32_t offset;
    uint32_t length;
  };
  std::vector<RawSection> sections;
  for (uint16_t i = 0; i < sections_count; ++i) {
    size_t entry_off = section_table_offset + i * 12;
    if (entry_off + 12 > data.size()) {
      XELOGE("XurFile: section table entry {} out of bounds", i);
      return nullptr;
    }
    uint32_t magic_val = read_be32(entry_off);
    char magic_chars[5] = {static_cast<char>((magic_val >> 24) & 0xFF),
                           static_cast<char>((magic_val >> 16) & 0xFF),
                           static_cast<char>((magic_val >> 8) & 0xFF),
                           static_cast<char>(magic_val & 0xFF), 0};
    sections.push_back({std::string(magic_chars), read_be32(entry_off + 4),
                        read_be32(entry_off + 8)});
  }

  auto xur = std::make_unique<XurFile>();

  const RawSection* strn_section = nullptr;
  const RawSection* flot_section = nullptr;
  const RawSection* vect_section = nullptr;
  const RawSection* quat_section = nullptr;
  const RawSection* colr_section = nullptr;
  const RawSection* data_section = nullptr;
  for (auto& section : sections) {
    if (section.magic == "STRN") strn_section = &section;
    else if (section.magic == "FLOT") flot_section = &section;
    else if (section.magic == "VECT") vect_section = &section;
    else if (section.magic == "QUAT") quat_section = &section;
    else if (section.magic == "COLR") colr_section = &section;
    else if (section.magic == "DATA") data_section = &section;
  }

  if (strn_section) {
    if (strn_section->offset + strn_section->length > data.size()) {
      XELOGE("XurFile: STRN section out of bounds");
      return nullptr;
    }
    // Mini-header: total byte count (4 bytes), string count (2 bytes),
    // then that many nul-terminated strings - verified against nuihud.xur.
    Cursor strn_cursor(data, strn_section->offset);
    strn_cursor.ReadU8();
    strn_cursor.ReadU8();
    strn_cursor.ReadU8();
    strn_cursor.ReadU8();
    uint16_t string_count = (uint16_t(strn_cursor.ReadU8()) << 8) |
                            uint16_t(strn_cursor.ReadU8());
    for (uint16_t i = 0; i < string_count; ++i) {
      xur->strings_.push_back(strn_cursor.ReadCString());
      if (!strn_cursor.ok()) {
        XELOGE("XurFile: STRN truncated at string {}", i);
        return nullptr;
      }
    }
  }

  if (flot_section) {
    if (flot_section->offset + flot_section->length > data.size()) {
      XELOGE("XurFile: FLOT section out of bounds");
      return nullptr;
    }
    // Flat array of big-endian float32, verified against nuihud.xur
    // (852.0, 480.0, 240.0 - sane canvas/element dimensions).
    for (uint32_t off = flot_section->offset;
        off + 4 <= flot_section->offset + flot_section->length; off += 4) {
      uint32_t bits = read_be32(off);
      float f;
      std::memcpy(&f, &bits, sizeof(f));
      xur->floats_.push_back(f);
    }
  }

  if (vect_section) {
    if (vect_section->offset + vect_section->length > data.size()) {
      XELOGE("XurFile: VECT section out of bounds");
      return nullptr;
    }
    // 3x big-endian float32 per entry (x, y, z) - not yet verified against
    // a real file with a Vector property set, per XUIHelper's VECTSection.
    for (uint32_t off = vect_section->offset;
        off + 12 <= vect_section->offset + vect_section->length; off += 12) {
      std::array<float, 3> v;
      for (int i = 0; i < 3; ++i) {
        uint32_t bits = read_be32(off + i * 4);
        std::memcpy(&v[i], &bits, sizeof(float));
      }
      xur->vectors_.push_back(v);
    }
  }

  if (quat_section) {
    if (quat_section->offset + quat_section->length > data.size()) {
      XELOGE("XurFile: QUAT section out of bounds");
      return nullptr;
    }
    // 4x big-endian float32 per entry (x, y, z, w) - not yet verified
    // against a real file with a Quaternion property set.
    for (uint32_t off = quat_section->offset;
        off + 16 <= quat_section->offset + quat_section->length; off += 16) {
      std::array<float, 4> q;
      for (int i = 0; i < 4; ++i) {
        uint32_t bits = read_be32(off + i * 4);
        std::memcpy(&q[i], &bits, sizeof(float));
      }
      xur->quaternions_.push_back(q);
    }
  }

  if (colr_section) {
    if (colr_section->offset + colr_section->length > data.size()) {
      XELOGE("XurFile: COLR section out of bounds");
      return nullptr;
    }
    // 4 bytes ARGB per entry - not yet verified against a real file with a
    // Colour property set.
    for (uint32_t off = colr_section->offset;
        off + 4 <= colr_section->offset + colr_section->length; off += 4) {
      xur->colours_.push_back(read_be32(off));
    }
  }

  if (!data_section) {
    XELOGE("XurFile: no DATA section found");
    return nullptr;
  }
  if (data_section->offset + data_section->length > data.size()) {
    XELOGE("XurFile: DATA section out of bounds");
    return nullptr;
  }
  Cursor data_cursor(data, data_section->offset);
  xur->root_ = xur->ReadObject(&data_cursor, 0);
  if (!xur->root_) {
    return nullptr;
  }
  if (data_cursor.offset() != data_section->offset + data_section->length) {
    XELOGW("XurFile: DATA section had {} leftover bytes after parsing",
           (data_section->offset + data_section->length) -
               data_cursor.offset());
  }

  return xur;
}

namespace {
void LogObject(const XurObject* object, int depth) {
  std::string indent(depth * 2, ' ');
  std::string props;
  for (size_t i = 0; i < object->properties.size(); ++i) {
    if (i) props += ", ";
    props += object->properties[i].name + "=" + object->properties[i].display_value;
  }
  XELOGI("{}{} [{}]", indent, object->class_name, props);
  for (auto& child : object->children) {
    LogObject(child.get(), depth + 1);
  }
}
}  // namespace

void XurFile::LogTree() const {
  if (!root_) {
    XELOGI("XurFile: (no root object)");
    return;
  }
  LogObject(root_.get(), 0);
}

namespace {
const XurResolvedProperty* FindProperty(const XurObject* object,
                                        const char* name) {
  for (auto& prop : object->properties) {
    if (prop.name == name) return &prop;
  }
  return nullptr;
}

void WalkLayout(const XurObject* object, float parent_x, float parent_y,
                int depth, std::vector<XurLayoutRect>* out) {
  float width = 0, height = 0;
  if (auto* w = FindProperty(object, "Width")) width = w->x;
  if (auto* h = FindProperty(object, "Height")) height = h->x;
  float x = parent_x, y = parent_y;
  if (auto* pos = FindProperty(object, "Position")) {
    x += pos->x;
    y += pos->y;
  }
  out->push_back({x, y, width, height, depth, object->class_name});
  for (auto& child : object->children) {
    WalkLayout(child.get(), x, y, depth + 1, out);
  }
}
}  // namespace

std::vector<XurLayoutRect> ComputeXurLayout(const XurObject* root) {
  std::vector<XurLayoutRect> result;
  if (root) {
    WalkLayout(root, 0, 0, 0, &result);
  }
  return result;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
