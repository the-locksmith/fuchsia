// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/crashpad/crashpad_analyzer_impl.h"

#include <stdio.h>

#include <map>
#include <string>
#include <utility>

#include <fuchsia/crash/cpp/fidl.h>
#include <lib/fxl/logging.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "garnet/bin/crashpad/config.h"
#include "garnet/bin/crashpad/report_annotations.h"
#include "garnet/bin/crashpad/report_attachments.h"
#include "garnet/bin/crashpad/scoped_unlink.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/client/settings.h"
#include "third_party/crashpad/handler/fuchsia/crash_report_exception_handler.h"
#include "third_party/crashpad/handler/minidump_to_upload_parameters.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"
#include "third_party/crashpad/third_party/mini_chromium/mini_chromium/base/files/file_path.h"
#include "third_party/crashpad/third_party/mini_chromium/mini_chromium/base/files/scoped_file.h"
#include "third_party/crashpad/util/file/file_io.h"
#include "third_party/crashpad/util/file/file_reader.h"
#include "third_party/crashpad/util/misc/metrics.h"
#include "third_party/crashpad/util/misc/uuid.h"
#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace fuchsia {
namespace crash {
namespace {

const char kDefaultConfigPath[] = "/pkg/data/default_config.json";
const char kOverrideConfigPath[] =
    "/system/data/crashpad_analyzer/override_config.json";

std::string GetPackageName(const zx::process& process) {
  char name[ZX_MAX_NAME_LEN];
  if (process.get_property(ZX_PROP_NAME, name, sizeof(name)) == ZX_OK) {
    return std::string(name);
  }
  return std::string("unknown-package");
}

}  // namespace

zx_status_t CrashpadAnalyzerImpl::UploadReport(
    const crashpad::UUID& local_report_id,
    const std::map<std::string, std::string>* annotations,
    bool read_annotations_from_minidump) {
  bool uploads_enabled;
  if ((!database_->GetSettings()->GetUploadsEnabled(&uploads_enabled) ||
       !uploads_enabled)) {
    FX_LOGS(INFO)
        << "upload to remote crash server disabled. Local crash report, ID "
        << local_report_id.ToString() << ", available under "
        << config_.local_crashpad_database_path;
    database_->SkipReportUpload(
        local_report_id,
        crashpad::Metrics::CrashSkippedReason::kUploadsDisabled);
    return ZX_OK;
  }

  // Read local crash report as an "upload" report.
  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport> report;
  const crashpad::CrashReportDatabase::OperationStatus database_status =
      database_->GetReportForUploading(local_report_id, &report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error loading local crash report, ID "
                   << local_report_id.ToString() << " (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  // Set annotations, either from argument or from minidump.
  FXL_CHECK((annotations != nullptr) ^ read_annotations_from_minidump);
  const std::map<std::string, std::string>* final_annotations = annotations;
  std::map<std::string, std::string> minidump_annotations;
  if (read_annotations_from_minidump) {
    crashpad::FileReader* reader = report->Reader();
    crashpad::FileOffset start_offset = reader->SeekGet();
    crashpad::ProcessSnapshotMinidump minidump_process_snapshot;
    if (!minidump_process_snapshot.Initialize(reader)) {
      database_->SkipReportUpload(
          report->uuid,
          crashpad::Metrics::CrashSkippedReason::kPrepareForUploadFailed);
      FX_LOGS(ERROR) << "error processing minidump for local crash report, ID "
                     << local_report_id.ToString();
      return ZX_ERR_INTERNAL;
    }
    minidump_annotations = crashpad::BreakpadHTTPFormParametersFromMinidump(
        &minidump_process_snapshot);
    final_annotations = &minidump_annotations;
    if (!reader->SeekSet(start_offset)) {
      database_->SkipReportUpload(
          report->uuid,
          crashpad::Metrics::CrashSkippedReason::kPrepareForUploadFailed);
      FX_LOGS(ERROR) << "error processing minidump for local crash report, ID "
                     << local_report_id.ToString();
      return ZX_ERR_INTERNAL;
    }
  }

  // We have to build the MIME multipart message ourselves as all the public
  // Crashpad helpers are asynchronous and we won't be able to know the upload
  // status nor the server report ID.
  crashpad::HTTPMultipartBuilder http_multipart_builder;
  http_multipart_builder.SetGzipEnabled(true);
  for (const auto& kv : *final_annotations) {
    http_multipart_builder.SetFormData(kv.first, kv.second);
  }
  for (const auto& kv : report->GetAttachments()) {
    http_multipart_builder.SetFileAttachment(kv.first, kv.first, kv.second,
                                             "application/octet-stream");
  }
  http_multipart_builder.SetFileAttachment(
      "uploadFileMinidump", report->uuid.ToString() + ".dmp", report->Reader(),
      "application/octet-stream");

  std::unique_ptr<crashpad::HTTPTransport> http_transport(
      crashpad::HTTPTransport::Create());
  crashpad::HTTPHeaders content_headers;
  http_multipart_builder.PopulateContentHeaders(&content_headers);
  for (const auto& content_header : content_headers) {
    http_transport->SetHeader(content_header.first, content_header.second);
  }
  http_transport->SetBodyStream(http_multipart_builder.GetBodyStream());
  http_transport->SetTimeout(60.0);  // 1 minute.
  http_transport->SetURL(*config_.crash_server_url);

  std::string server_report_id;
  if (!http_transport->ExecuteSynchronously(&server_report_id)) {
    database_->SkipReportUpload(
        report->uuid, crashpad::Metrics::CrashSkippedReason::kUploadFailed);
    FX_LOGS(ERROR) << "error uploading local crash report, ID "
                   << report->uuid.ToString();
    return ZX_ERR_INTERNAL;
  }
  database_->RecordUploadComplete(std::move(report), server_report_id);
  FX_LOGS(INFO) << "successfully uploaded crash report at "
                   "https://crash.corp.google.com/"
                << server_report_id;

  return ZX_OK;
}

zx_status_t CrashpadAnalyzerImpl::HandleNativeException(
    zx::process process, zx::thread thread, zx::port exception_port) {
  const std::string package_name = GetPackageName(process);
  FX_LOGS(INFO) << "generating crash report for exception thrown by "
                << package_name;

  // Prepare annotations and attachments.
  const std::map<std::string, std::string> annotations =
      MakeDefaultAnnotations(package_name);
  // The Crashpad exception handler expects filepaths for the passed
  // attachments, not file objects, but we need the underlying files
  // to still be there.
  std::map<std::string, base::FilePath> attachments;
  const ScopedUnlink tmp_kernel_log_file(
      WriteKernelLogToFile(config_.local_crashpad_database_path));
  if (tmp_kernel_log_file.is_valid()) {
    attachments[kAttachmentKernelLog] =
        base::FilePath(tmp_kernel_log_file.get());
  }
  attachments[kAttachmentBuildInfoSnapshot] =
      base::FilePath("/config/build-info/snapshot");
  // TODO(DX-581): attach syslog as well.

  // Set minidump and create local crash report.
  //   * The annotations will be stored in the minidump of the report and
  //     augmented with modules' annotations.
  //   * The attachments will be stored in the report.
  // We don't pass an upload_thread so we can do the upload ourselves
  // synchronously.
  crashpad::CrashReportExceptionHandler exception_handler(
      database_.get(), /*upload_thread=*/nullptr, &annotations, &attachments,
      /*user_stream_data_sources=*/nullptr);
  crashpad::UUID local_report_id;
  if (!exception_handler.HandleExceptionHandles(
          process, thread, zx::unowned_port(exception_port),
          &local_report_id)) {
    database_->SkipReportUpload(
        local_report_id,
        crashpad::Metrics::CrashSkippedReason::kPrepareForUploadFailed);
    FX_LOGS(ERROR) << "error handling exception for local crash report, ID "
                   << local_report_id.ToString();
    return ZX_ERR_INTERNAL;
  }

  // For userspace, we read back the annotations from the minidump instead of
  // passing them as argument like for kernel crashes because the Crashpad
  // handler augmented them with the modules' annotations.
  return UploadReport(local_report_id, /*annotations=*/nullptr,
                      /*read_annotations_from_minidump=*/true);
}

zx_status_t CrashpadAnalyzerImpl::HandleManagedRuntimeException(
    ManagedRuntimeLanguage language, std::string component_url,
    std::string exception, fuchsia::mem::Buffer stack_trace) {
  FX_LOGS(INFO) << "generating crash report for exception thrown by "
                << component_url;

  crashpad::CrashReportDatabase::OperationStatus database_status;

  // Create local crash report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  database_status = database_->PrepareNewCrashReport(&report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error creating local crash report (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  // Prepare annotations and attachments.
  const std::map<std::string, std::string> annotations =
      MakeManagedRuntimeExceptionAnnotations(language, component_url,
                                             exception);
  if (AddManagedRuntimeExceptionAttachments(report.get(), language,
                                            std::move(stack_trace)) != ZX_OK) {
    FX_LOGS(WARNING) << "error adding attachments to local crash report";
  }

  // Finish new local crash report.
  crashpad::UUID local_report_id;
  database_status = database_->FinishedWritingCrashReport(std::move(report),
                                                          &local_report_id);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error writing local crash report (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  return UploadReport(local_report_id, &annotations,
                      /*read_annotations_from_minidump=*/false);
}

zx_status_t CrashpadAnalyzerImpl::ProcessKernelPanicCrashlog(
    fuchsia::mem::Buffer crashlog) {
  FX_LOGS(INFO) << "generating crash report for previous kernel panic";

  crashpad::CrashReportDatabase::OperationStatus database_status;

  // Create local crash report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  database_status = database_->PrepareNewCrashReport(&report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error creating local crash report (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  // Prepare annotations and attachments.
  const std::map<std::string, std::string> annotations =
      MakeDefaultAnnotations(/*package_name=*/"kernel");
  if (AddKernelPanicAttachments(report.get(), std::move(crashlog)) != ZX_OK) {
    FX_LOGS(WARNING) << "error adding attachments to local crash report";
  }

  // Finish new local crash report.
  crashpad::UUID local_report_id;
  database_status = database_->FinishedWritingCrashReport(std::move(report),
                                                          &local_report_id);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error writing local crash report (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  return UploadReport(local_report_id, &annotations,
                      /*read_annotations_from_minidump=*/false);
}

CrashpadAnalyzerImpl::CrashpadAnalyzerImpl(
    Config config, std::unique_ptr<crashpad::CrashReportDatabase> database)
    : config_(std::move(config)), database_(std::move(database)) {
  FXL_DCHECK(database_);
}

void CrashpadAnalyzerImpl::HandleNativeException(
    zx::process process, zx::thread thread, zx::port exception_port,
    HandleNativeExceptionCallback callback) {
  const zx_status_t status = HandleNativeException(
      std::move(process), std::move(thread), std::move(exception_port));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to handle native exception. Won't retry.";
  }
  callback(status);
}

void CrashpadAnalyzerImpl::HandleManagedRuntimeException(
    ManagedRuntimeLanguage language, std::string component_url,
    std::string exception, fuchsia::mem::Buffer stack_trace,
    HandleManagedRuntimeExceptionCallback callback) {
  const zx_status_t status = HandleManagedRuntimeException(
      language, component_url, exception, std::move(stack_trace));
  if (status != ZX_OK) {
    FX_LOGS(ERROR)
        << "failed to handle managed runtime exception. Won't retry.";
  }
  callback(status);
}

void CrashpadAnalyzerImpl::ProcessKernelPanicCrashlog(
    fuchsia::mem::Buffer crashlog,
    ProcessKernelPanicCrashlogCallback callback) {
  const zx_status_t status = ProcessKernelPanicCrashlog(std::move(crashlog));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to process kernel panic crashlog. Won't retry.";
  }
  callback(status);
}

std::unique_ptr<CrashpadAnalyzerImpl> CrashpadAnalyzerImpl::TryCreate(
    Config config) {
  if (!files::IsDirectory(config.local_crashpad_database_path)) {
    files::CreateDirectory(config.local_crashpad_database_path);
  }

  std::unique_ptr<crashpad::CrashReportDatabase> database(
      crashpad::CrashReportDatabase::Initialize(
          base::FilePath(config.local_crashpad_database_path)));
  if (!database) {
    FX_LOGS(ERROR) << "error initializing local crash report database at "
                   << config.local_crashpad_database_path;
    FX_LOGS(FATAL) << "failed to set up crash analyzer";
    return nullptr;
  }

  // Today we enable uploads here. In the future, this will most likely be set
  // in some external settings.
  database->GetSettings()->SetUploadsEnabled(
      config.enable_upload_to_crash_server);

  return std::unique_ptr<CrashpadAnalyzerImpl>(
      new CrashpadAnalyzerImpl(std::move(config), std::move(database)));
}

std::unique_ptr<CrashpadAnalyzerImpl> CrashpadAnalyzerImpl::TryCreate() {
  Config config;

  if (files::IsFile(kOverrideConfigPath) &&
      ParseConfig(kOverrideConfigPath, &config) == ZX_OK) {
    return CrashpadAnalyzerImpl::TryCreate(std::move(config));
  }

  // We try to load the default config included in the package if no override
  // config was specified or we failed to parse it.
  if (ParseConfig(kDefaultConfigPath, &config) == ZX_OK) {
    return CrashpadAnalyzerImpl::TryCreate(std::move(config));
  }

  FX_LOGS(FATAL) << "failed to set up crash analyzer";
  return nullptr;
}

}  // namespace crash
}  // namespace fuchsia
