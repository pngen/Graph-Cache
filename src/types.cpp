#include "graphcache/types.hpp"

namespace gc {

const char* to_string(BackendKind k) noexcept {
  switch (k) {
    case BackendKind::Cpu: return "cpu";
    case BackendKind::Cuda: return "cuda";
  }
  return "unknown";
}
const char* to_string(AcceleratorVendor v) noexcept {
  switch (v) {
    case AcceleratorVendor::Cpu: return "cpu";
    case AcceleratorVendor::Nvidia: return "nvidia";
    case AcceleratorVendor::Amd: return "amd";
    case AcceleratorVendor::Intel: return "intel";
    case AcceleratorVendor::Generic: return "generic";
  }
  return "unknown";
}
const char* to_string(Datatype d) noexcept {
  switch (d) {
    case Datatype::None: return "none";
    case Datatype::F32: return "f32";
    case Datatype::F16: return "f16";
    case Datatype::BF16: return "bf16";
    case Datatype::F64: return "f64";
    case Datatype::I8: return "i8";
    case Datatype::I16: return "i16";
    case Datatype::I32: return "i32";
    case Datatype::I64: return "i64";
    case Datatype::U8: return "u8";
    case Datatype::U16: return "u16";
    case Datatype::U32: return "u32";
    case Datatype::U64: return "u64";
    case Datatype::Bool: return "bool";
  }
  return "unknown";
}
const char* to_string(TensorLayout l) noexcept {
  switch (l) {
    case TensorLayout::Contiguous: return "contiguous";
    case TensorLayout::RowMajor: return "row_major";
    case TensorLayout::ColumnMajor: return "column_major";
    case TensorLayout::NCHW: return "nchw";
    case TensorLayout::NHWC: return "nhwc";
    case TensorLayout::NCDHW: return "ncdhw";
    case TensorLayout::NDHWC: return "ndhwc";
    case TensorLayout::CustomBlocked: return "custom_blocked";
  }
  return "unknown";
}
const char* to_string(BindingClass b) noexcept {
  switch (b) {
    case BindingClass::ImmutableBinding: return "immutable";
    case BindingClass::ReplayMutableBinding: return "replay_mutable";
    case BindingClass::RecaptureRequiredBinding: return "recapture_required";
    case BindingClass::BackendValidatedBinding: return "backend_validated";
  }
  return "unknown";
}
const char* to_string(CaptureMode c) noexcept {
  switch (c) {
    case CaptureMode::None: return "none";
    case CaptureMode::StreamCapture: return "stream_capture";
    case CaptureMode::LegacyStreamCapture: return "legacy_stream_capture";
    case CaptureMode::ThreadLocalCapture: return "thread_local_capture";
    case CaptureMode::RelaxedStreamCapture: return "relaxed_stream_capture";
    case CaptureMode::DestroyStreamCapture: return "destroy_stream_capture";
    case CaptureMode::BackendManaged: return "backend_managed";
  }
  return "none";
}
const char* to_string(StreamSemantics s) noexcept {
  switch (s) {
    case StreamSemantics::Default: return "default";
    case StreamSemantics::StreamOrdered: return "stream_ordered";
    case StreamSemantics::Contextified: return "contextified";
    case StreamSemantics::BackendSpecific: return "backend_specific";
  }
  return "default";
}
const char* to_string(NodeCategory c) noexcept {
  switch (c) {
    case NodeCategory::Kernel: return "kernel";
    case NodeCategory::MemoryCopy: return "memory_copy";
    case NodeCategory::MemorySet: return "memory_set";
    case NodeCategory::HostOperation: return "host_operation";
    case NodeCategory::Synchronization: return "synchronization";
    case NodeCategory::ChildGraph: return "child_graph";
    case NodeCategory::EventPrimitive: return "event_primitive";
    case NodeCategory::BackendOpaque: return "backend_opaque";
  }
  return "unknown";
}

} // namespace gc
