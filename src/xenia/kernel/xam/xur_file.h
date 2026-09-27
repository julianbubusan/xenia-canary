/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XUR_FILE_H_
#define XENIA_KERNEL_XAM_XUR_FILE_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xe {
namespace kernel {
namespace xam {

// Reader/object-tree model for the XUR8 compiled XUI resource format
// ("XUIB" magic, version 8). Reverse engineered from the open-source
// NGxDTV/XZP-Tool-v3 (.xus) and SGCSam/XUIHelper (.xur) projects, then
// independently verified byte-for-byte against a real console-extracted
// file (xam.xzp's nuihud.xur) before being ported here.
//
// Every property value on the wire is a packed uint (or, for Bool, a
// single raw byte byte-identical to a 1-byte packed uint for any real
// true/false value) - structural tree walking only needs each class's own
// property count, so that part never needed per-property type info. Fully
// resolving a property's real value (as opposed to just its raw wire
// value) does need its type, since e.g. a Float property's packed-uint
// value is an INDEX into the FLOT section, not the value itself.

enum class XurPropertyType {
  kUnknown,
  kString,      // Index into STRN.
  kFloat,       // Index into FLOT.
  kInteger,     // Direct value (signed).
  kUnsigned,    // Direct value.
  kBool,        // Direct 0/1.
  kVector,      // Index into VECT (3 floats).
  kQuaternion,  // Index into QUAT (4 floats).
  kColour,      // Index into COLR (ARGB).
  kObject,      // Compound/object index - not yet resolved.
  kCustom,      // CUST section offset - not yet resolved.
};

struct XurResolvedProperty {
  std::string name;
  uint32_t raw_value;
  std::string display_value;  // Human-readable resolved value.
  // Populated for kFloat (x only) / kVector (x,y,z) so layout code can use
  // the real numeric value without re-touching section data directly.
  float x = 0, y = 0, z = 0;
};

struct XurObject {
  std::string class_name;
  std::vector<XurResolvedProperty> properties;
  std::vector<std::unique_ptr<XurObject>> children;
};

// A flattened, screen-space box for one object in the tree, for a very
// first-pass visualization. Real XUI layout (Anchor/Pivot/Scale/percentage
// positioning/etc.) isn't implemented - this just reads each object's own
// Width/Height (and Position if present, defaulting to the parent's
// origin otherwise) and nests children inside their parent's box
// unmodified. Good enough to see real element bounds on screen; not a
// faithful layout engine.
struct XurLayoutRect {
  float x, y, width, height;
  int depth;
  std::string class_name;
};

// Walks `root` computing XurLayoutRect entries via a simple depth-first,
// parent-relative accumulation (see XurLayoutRect's comment for caveats).
std::vector<XurLayoutRect> ComputeXurLayout(const XurObject* root);

class XurFile {
 public:
  // Parses raw file bytes. Returns nullptr and logs on any structural
  // failure (bad magic/version, truncated data, unknown class name, a
  // propertiesCount mismatch, etc.) - this is intentionally strict rather
  // than best-effort, since a silent wrong parse of real scene data would
  // be worse than a loud failure while the class schema table is still
  // small.
  static std::unique_ptr<XurFile> Parse(const std::vector<uint8_t>& data);

  const std::vector<std::string>& strings() const { return strings_; }
  XurObject* root() const { return root_.get(); }

  // Logs the full tree via XELOGI, indented by depth.
  void LogTree() const;

 private:
  class Cursor;
  std::unique_ptr<XurObject> ReadObject(Cursor* cursor, int depth);
  XurResolvedProperty ResolveProperty(const std::string& name,
                                      XurPropertyType type,
                                      uint32_t raw_value) const;

  std::vector<std::string> strings_;
  std::vector<float> floats_;
  std::vector<std::array<float, 3>> vectors_;
  std::vector<std::array<float, 4>> quaternions_;
  std::vector<uint32_t> colours_;  // Packed ARGB.
  std::unique_ptr<XurObject> root_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XUR_FILE_H_
