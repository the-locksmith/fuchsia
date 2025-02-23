// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Implementation = `
{{- define "Implementation" -}}
// WARNING: This file is machine generated by fidlgen.

#include <{{ .PrimaryHeader }}>

#include "lib/fidl/cpp/internal/implementation.h"

{{- range .Library }}
namespace {{ . }} {
{{- end }}
{{ "" }}

{{- range .Decls }}
{{- if Eq .Kind Kinds.Const }}{{ template "ConstDefinition" . }}{{- end }}
{{- if Eq .Kind Kinds.Interface }}{{ template "DispatchInterfaceDefinition" . }}{{- end }}
{{- if Eq .Kind Kinds.Struct }}{{ template "StructDefinition" . }}{{- end }}
{{- if Eq .Kind Kinds.Union }}{{ template "UnionDefinition" . }}{{- end }}
{{- if Eq .Kind Kinds.XUnion }}{{ template "XUnionDefinition" . }}{{- end }}
{{- if Eq .Kind Kinds.Table }}{{ template "TableDefinition" . }}{{- end }}
{{- end }}

{{- range .LibraryReversed }}
}  // namespace {{ . }}
{{- end }}
{{ end }}

{{- define "DispatchInterfaceDefinition" -}}
{{- range $transport, $_ := .Transports }}
{{- if Eq $transport "Channel" -}}{{ template "InterfaceDefinition" $ }}{{- end }}
{{- if Eq $transport "OvernetStream" }}{{ template "OvernetStreamDefinition" $ }}{{- end }}
{{- if Eq $transport "SocketControl" -}}{{ template "InterfaceDefinition" $ }}{{- end }}
{{- end }}
{{- end -}}
`
