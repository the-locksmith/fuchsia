// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_settings.h"

#include "garnet/bin/zxdb/client/setting_schema.h"
#include "garnet/bin/zxdb/client/setting_store.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/strings/join_strings.h"

namespace zxdb {

namespace {

std::string SettingValueToString(const SettingValue& value) {
  switch (value.type()) {
    case SettingType::kBoolean:
      return value.GetBool() ? "true" : "false";
    case SettingType::kInteger:
      return fxl::StringPrintf("%d", value.GetInt());
    case SettingType::kString: {
      auto string = value.GetString();
      return string.empty() ? "<empty>" : string;
    }
    case SettingType::kList:
      // Lists are formatted as a colon separated string.
      // Example
      //    list = {"first", "second", "third"} -> "first:second:third"
      return fxl::JoinStrings(value.GetList(), ":");
    case SettingType::kNull:
      return "<null>";
  }
}

std::vector<std::string> ListToBullet(const std::vector<std::string>& list) {
  std::vector<std::string> output;
  output.reserve(list.size());
  auto bullet = GetBullet();
  for (const std::string& item : list)
    output.emplace_back(fxl::StringPrintf("%s %s", bullet.data(), item.data()));
  return output;
}

// |add_heading| refers whether it should show the setting name or just list the
// values.
void AddSettingToTable(const StoredSetting& setting,
                       std::vector<std::vector<OutputBuffer>>* rows,
                       bool add_heading = true) {
  // TODO(donosoc): We need to check what level the setting comes from so we can
  //                highlight it in the listing.
  if (!setting.value.is_list()) {
    // Normal values as just entered as key-value pairs.
    auto& row = rows->emplace_back();
    if (add_heading)
      row.emplace_back(setting.schema_item.name());
    row.emplace_back(SettingValueToString(setting.value));
  } else {
    // List get special treatment so that we can show them as bullet lists.
    // This make reading them much easier when the elements of the lists
    // are long (eg. paths).
    auto bullet_list = ListToBullet(setting.value.GetList());
    // Special case for empty list.
    if (bullet_list.empty()) {
      auto& row = rows->emplace_back();
      if (add_heading)
        row.emplace_back(setting.schema_item.name());
      row.emplace_back("<empty>");
    } else {
      for (size_t i = 0; i < bullet_list.size(); i++) {
        auto& row = rows->emplace_back();

        if (add_heading) {
          // The first entry has the setting name.
          auto title = i == 0 ? OutputBuffer(setting.schema_item.name())
                              : OutputBuffer();
          auto it = row.emplace_back(std::move(title));
        }
        row.emplace_back(std::move(bullet_list[i]));
      }
    }
  }
}

OutputBuffer FormatSettingStore(const SettingStore& store) {
  std::vector<std::vector<OutputBuffer>> rows;
  for (auto [key, item] : store.schema()->items()) {
    // Overridden settings are meant to be listen in another schema.
    if (item.overriden())
      continue;

    auto setting = store.GetSetting(key);
    FXL_DCHECK(!setting.value.is_null());

    AddSettingToTable(setting, &rows);
  }

  OutputBuffer table;
  FormatTable(std::vector<ColSpec>(3), rows, &table);
  return table;
}

OutputBuffer FormatSchema(const SettingSchema& schema) {
  std::vector<std::vector<OutputBuffer>> rows;
  for (auto [key, item] : schema.items()) {
    // Overridden settings are meant to be listen in another schema.
    if (item.overriden())
      continue;

    StoredSetting setting;
    setting.schema_item = schema.GetItem(key);
    setting.value = setting.schema_item.value();
    FXL_DCHECK(!setting.value.is_null());
    AddSettingToTable(setting, &rows);
  }

  OutputBuffer table;
  FormatTable(std::vector<ColSpec>(3), rows, &table);
  return table;
}

}  // namespace

void FormatSettings(const Command& cmd, OutputBuffer* out) {
  // We print each setting
  out->Append({Syntax::kHeading, "\nSystem\n"});
  out->Append(FormatSettingStore(cmd.target()->session()->system().settings()));

  OutputBuffer current;
  JobContext* job = cmd.job_context();
  current = job ? FormatSettingStore(job->settings())
                : FormatSchema(*JobContext::GetSchema());
  if (!current.empty()) {
    out->Append({Syntax::kHeading, "\nJob\n"});
    out->Append(current);
  }

  Target* target = cmd.target();
  current = target ? FormatSettingStore(target->settings())
                   : FormatSchema(*Target::GetSchema());
  if (!current.empty()) {
    out->Append({Syntax::kHeading, "\nTarget\n"});
    out->Append(current);
  }

  Thread* thread = cmd.thread();
  current = thread ? FormatSettingStore(thread->settings())
                   : FormatSchema(*Thread::GetSchema());
  if (!current.empty()) {
    out->Append({Syntax::kHeading, "\nThread\n"});
    out->Append(current);
  }
}

Err FormatSetting(const Command& cmd, const std::string& setting_name,
                  OutputBuffer* out) {
  // We look from more specific to less specific for the setting.
  Thread* thread = cmd.thread();
  if (thread && thread->settings().HasSetting(setting_name)) {
    OutputBuffer tmp;
    Err err = FormatSetting(thread->settings(), setting_name, &tmp);
    out->Append(std::move(tmp));
    return err;
  }

  Target* target = cmd.target();
  if (target && target->settings().HasSetting(setting_name)) {
    OutputBuffer tmp;
    Err err = FormatSetting(target->settings(), setting_name, &tmp);
    out->Append(std::move(tmp));
    return err;
  }

  JobContext* job = cmd.job_context();
  if (job && job->settings().HasSetting(setting_name)) {
    OutputBuffer tmp;
    Err err = FormatSetting(job->settings(), setting_name, &tmp);
    out->Append(std::move(tmp));
    return err;
  }

  System& system = cmd.target()->session()->system();
  if (system.settings().HasSetting(setting_name)) {
    OutputBuffer tmp;
    Err err = FormatSetting(system.settings(), setting_name, &tmp);
    out->Append(std::move(tmp));
    return err;
  }

  return Err("Could not find setting \"%s\"", setting_name.data());
}

Err FormatSetting(const SettingStore& store, const std::string& setting_name,
                  OutputBuffer* out) {
  if (!store.HasSetting(setting_name))
    return Err("Could not find setting \"%s\"", setting_name.data());

  auto setting = store.GetSetting(setting_name);

  out->Append({Syntax::kHeading, setting.schema_item.name()});
  out->Append(OutputBuffer("\n"));

  out->Append(setting.schema_item.description());
  out->Append(OutputBuffer("\n\n"));

  out->Append({Syntax::kHeading, "Type: "});
  out->Append(SettingTypeToString(setting.schema_item.type()));
  out->Append("\n\n");

  out->Append({Syntax::kHeading, "Value(s):\n"});
  out->Append(FormatSettingValue(setting));

  // List have a copy-paste value for setting the value.
  if (setting.value.is_list()) {
    out->Append("\n");
    out->Append({Syntax::kComment,
                 "See \"help set\" about using the set value for lists.\n"});
    out->Append(fxl::StringPrintf("Set value: %s",
                                  SettingValueToString(setting.value).data()));
    out->Append("\n");
  }
  return Err();
}

OutputBuffer FormatSettingValue(const StoredSetting& setting) {
  FXL_DCHECK(!setting.value.is_null());

  OutputBuffer out;
  std::vector<std::vector<OutputBuffer>> rows;
  AddSettingToTable(setting, &rows, false);
  FormatTable(std::vector<ColSpec>{1}, std::move(rows), &out);
  return out;
}

const char* SettingSchemaLevelToString(SettingSchema::Level level) {
  switch (level) {
    case SettingSchema::Level::kDefault:
      return "default";
    case SettingSchema::Level::kSystem:
      return "system";
    case SettingSchema::Level::kJob:
      return "job";
    case SettingSchema::Level::kTarget:
      return "target";
    case SettingSchema::Level::kThread:
      return "thread";
  }

  // Just in case.
  return "<invalid>";
}

}  // namespace zxdb
