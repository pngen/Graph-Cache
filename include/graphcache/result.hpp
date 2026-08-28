#pragma once
// Structured result and error types.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include <string>
#include <variant>
#include <utility>

namespace gc {

// Generic, materially distinct error codes. The compatibility decision classes
// are a separate enumeration (see compatibility.hpp) since a compatible result
// is not an error; only the incompatible/invalid/stale classes surface here.
enum class ErrorCode : int {
  Ok = 0,
  InvalidArgument,
  OperationNotSupported,
  BackendUnavailable,
  DeviceUnavailable,
  OutOfMemory,
  NoSuchGraph,
  NoSuchArtifact,
  LookupMiss,
  CaptureInProgress,
  DuplicateCapture,
  CaptureFailed,
  ValidationFailed,
  IllegalTransition,
  MissingCapture,
  NotResident,
  ResidencyPressure,
  EvictionImpossible,
  PersistenceIoError,
  PersistenceCorrupt,
  PersistenceTruncated,
  PersistenceUnknownVersion,
  PersistenceTrailingGarbage,
  PersistenceChecksumMismatch,
  PersistenceNotFound,
  RecoveryIncomplete,
  ProtocolIoError,
  ProtocolMalformed,
  ProtocolUnknownVersion,
  ProtocolUnknownMessage,
  ProtocolOverflow,
  ProtocolTruncated,
  ProtocolZeroLength,
  GenerationMismatch,
  StaleAuthority,
  StaleEpoch,
  StaleWorkerBoot,
  StaleCacheGeneration,
  StaleGraphGeneration,
  StaleCaptureAttempt,
  StaleReplayAttempt,
  StaleValidation,
  StaleLoad,
  StaleCompletion,
  LeaseAlreadyHeld,
  LeaseUnderflow,
  LeaseNotHeld,
  Invalidated,
  DependencyInvalid,
  NamespaceNotFound,
  QuotaExceeded,
  TopologyInvalid,
  TopologyDanglingEdge,
  TopologySelfCycle,
  TopologyCycle,
  TopologyDuplicateNode,
  TopologyDuplicateEdge,
  TopologyDuplicateId,
  Contradictory,
  ImmutableViolation,
  IndexInconsistent,
  ConcurrentModification,
  Internal,
  Unknown
};

[[nodiscard]] const char* to_string(ErrorCode code) noexcept;

struct Error {
  ErrorCode code{ErrorCode::Unknown};
  std::string message;

  Error() = default;
  Error(ErrorCode c, std::string m) : code(c), message(std::move(m)) {}
  explicit Error(std::string m) : code(ErrorCode::Internal), message(std::move(m)) {}
  [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::Ok; }
};

template <class T>
class Result {
 public:
  Result() : data_(Error(ErrorCode::InvalidArgument, "empty result")) {}
  Result(T value) : data_(std::move(value)) {}
  Result(Error error) : data_(std::move(error)) {}

  [[nodiscard]] static Result success(T v) { return Result(std::move(v)); }
  [[nodiscard]] static Result failure(Error e) { return Result(std::move(e)); }

  [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }
  [[nodiscard]] const T& value() const {
    return std::get<T>(data_);
  }
  [[nodiscard]] const Error& error() const {
    return std::get<Error>(data_);
  }
  [[nodiscard]] const T* operator->() const { return &std::get<T>(data_); }
  [[nodiscard]] T* operator->() { return &std::get<T>(data_); }
  [[nodiscard]] const T& operator*() const { return std::get<T>(data_); }
  [[nodiscard]] T& operator*() { return std::get<T>(data_); }
  [[nodiscard]] T value_or(T fallback) const {
    return ok() ? std::get<T>(data_) : std::move(fallback);
  }
  // Rebind a value Result into a void Result.
  [[nodiscard]] Result<void> ignore_value() const;

 private:
  std::variant<T, Error> data_;
};

template <>
class Result<void> {
 public:
  Result() : ok_(true) {}
  Result(Error error) : ok_(false), error_(std::move(error)) {}

  [[nodiscard]] static Result success() { return Result(); }
  [[nodiscard]] static Result failure(Error e) { return Result(std::move(e)); }

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] const Error& error() const { return error_; }

 private:
  bool ok_{true};
  Error error_;
};

template <class T>
Result<void> Result<T>::ignore_value() const {
  if (ok()) return Result<void>::success();
  return Result<void>::failure(error());
}

} // namespace gc
