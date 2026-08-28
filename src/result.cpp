#include "graphcache/result.hpp"

namespace gc {

const char* to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok: return "Ok";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::OperationNotSupported: return "OperationNotSupported";
    case ErrorCode::BackendUnavailable: return "BackendUnavailable";
    case ErrorCode::DeviceUnavailable: return "DeviceUnavailable";
    case ErrorCode::OutOfMemory: return "OutOfMemory";
    case ErrorCode::NoSuchGraph: return "NoSuchGraph";
    case ErrorCode::NoSuchArtifact: return "NoSuchArtifact";
    case ErrorCode::LookupMiss: return "LookupMiss";
    case ErrorCode::CaptureInProgress: return "CaptureInProgress";
    case ErrorCode::DuplicateCapture: return "DuplicateCapture";
    case ErrorCode::CaptureFailed: return "CaptureFailed";
    case ErrorCode::ValidationFailed: return "ValidationFailed";
    case ErrorCode::IllegalTransition: return "IllegalTransition";
    case ErrorCode::MissingCapture: return "MissingCapture";
    case ErrorCode::NotResident: return "NotResident";
    case ErrorCode::ResidencyPressure: return "ResidencyPressure";
    case ErrorCode::EvictionImpossible: return "EvictionImpossible";
    case ErrorCode::PersistenceIoError: return "PersistenceIoError";
    case ErrorCode::PersistenceCorrupt: return "PersistenceCorrupt";
    case ErrorCode::PersistenceTruncated: return "PersistenceTruncated";
    case ErrorCode::PersistenceUnknownVersion: return "PersistenceUnknownVersion";
    case ErrorCode::PersistenceTrailingGarbage: return "PersistenceTrailingGarbage";
    case ErrorCode::PersistenceChecksumMismatch: return "PersistenceChecksumMismatch";
    case ErrorCode::PersistenceNotFound: return "PersistenceNotFound";
    case ErrorCode::RecoveryIncomplete: return "RecoveryIncomplete";
    case ErrorCode::ProtocolIoError: return "ProtocolIoError";
    case ErrorCode::ProtocolMalformed: return "ProtocolMalformed";
    case ErrorCode::ProtocolUnknownVersion: return "ProtocolUnknownVersion";
    case ErrorCode::ProtocolUnknownMessage: return "ProtocolUnknownMessage";
    case ErrorCode::ProtocolOverflow: return "ProtocolOverflow";
    case ErrorCode::ProtocolTruncated: return "ProtocolTruncated";
    case ErrorCode::ProtocolZeroLength: return "ProtocolZeroLength";
    case ErrorCode::GenerationMismatch: return "GenerationMismatch";
    case ErrorCode::StaleAuthority: return "StaleAuthority";
    case ErrorCode::StaleEpoch: return "StaleEpoch";
    case ErrorCode::StaleWorkerBoot: return "StaleWorkerBoot";
    case ErrorCode::StaleCacheGeneration: return "StaleCacheGeneration";
    case ErrorCode::StaleGraphGeneration: return "StaleGraphGeneration";
    case ErrorCode::StaleCaptureAttempt: return "StaleCaptureAttempt";
    case ErrorCode::StaleReplayAttempt: return "StaleReplayAttempt";
    case ErrorCode::StaleValidation: return "StaleValidation";
    case ErrorCode::StaleLoad: return "StaleLoad";
    case ErrorCode::StaleCompletion: return "StaleCompletion";
    case ErrorCode::LeaseAlreadyHeld: return "LeaseAlreadyHeld";
    case ErrorCode::LeaseUnderflow: return "LeaseUnderflow";
    case ErrorCode::LeaseNotHeld: return "LeaseNotHeld";
    case ErrorCode::Invalidated: return "Invalidated";
    case ErrorCode::DependencyInvalid: return "DependencyInvalid";
    case ErrorCode::NamespaceNotFound: return "NamespaceNotFound";
    case ErrorCode::QuotaExceeded: return "QuotaExceeded";
    case ErrorCode::TopologyInvalid: return "TopologyInvalid";
    case ErrorCode::TopologyDanglingEdge: return "TopologyDanglingEdge";
    case ErrorCode::TopologySelfCycle: return "TopologySelfCycle";
    case ErrorCode::TopologyCycle: return "TopologyCycle";
    case ErrorCode::TopologyDuplicateNode: return "TopologyDuplicateNode";
    case ErrorCode::TopologyDuplicateEdge: return "TopologyDuplicateEdge";
    case ErrorCode::TopologyDuplicateId: return "TopologyDuplicateId";
    case ErrorCode::Contradictory: return "Contradictory";
    case ErrorCode::ImmutableViolation: return "ImmutableViolation";
    case ErrorCode::IndexInconsistent: return "IndexInconsistent";
    case ErrorCode::ConcurrentModification: return "ConcurrentModification";
    case ErrorCode::Internal: return "Internal";
    case ErrorCode::Unknown: return "Unknown";
  }
  return "Unknown";
}

} // namespace gc
