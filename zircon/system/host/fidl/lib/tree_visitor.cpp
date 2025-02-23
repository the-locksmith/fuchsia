// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "fidl/raw_ast.h"
#include "fidl/tree_visitor.h"

namespace fidl {
namespace raw {

void DeclarationOrderTreeVisitor::OnFile(std::unique_ptr<File> const& element) {
    OnSourceElementStart(*element);

    if (element->attributes != nullptr) {
        OnAttributeList(element->attributes);
    }

    OnCompoundIdentifier(element->library_name);
    for (auto i = element->using_list.begin();
         i != element->using_list.end();
         ++i) {
        OnUsing(*i);
    }

    auto bits_decls_it = element->bits_declaration_list.begin();
    auto const_decls_it = element->const_declaration_list.begin();
    auto enum_decls_it = element->enum_declaration_list.begin();
    auto interface_decls_it = element->interface_declaration_list.begin();
    auto struct_decls_it = element->struct_declaration_list.begin();
    auto table_decls_it = element->table_declaration_list.begin();
    auto union_decls_it = element->union_declaration_list.begin();
    auto xunion_decls_it = element->xunion_declaration_list.begin();

    enum Next {
        bits_t,
        const_t,
        enum_t,
        interface_t,
        struct_t,
        table_t,
        union_t,
        xunion_t,
    };

    std::map<const char*, Next> m;
    for (;;) {
        // We want to visit these in declaration order, rather than grouped
        // by type of declaration.  std::map is sorted by key.  For each of
        // these lists of declarations, we make a map where the key is "the
        // next start location of the earliest element in the list" to a
        // variable representing the type.  We then identify which type was
        // put earliest in the map.  That will be the earliest declaration
        // in the file.  We then visit the declaration accordingly.
        m.clear();
        if (bits_decls_it != element->bits_declaration_list.end()) {
            m[(*bits_decls_it)->start_.previous_end().data().data()] = bits_t;
        }
        if (const_decls_it != element->const_declaration_list.end()) {
            m[(*const_decls_it)->start_.previous_end().data().data()] = const_t;
        }
        if (enum_decls_it != element->enum_declaration_list.end()) {
            m[(*enum_decls_it)->start_.previous_end().data().data()] = enum_t;
        }
        if (interface_decls_it != element->interface_declaration_list.end()) {
            if (*interface_decls_it == nullptr) {
                // Used to indicate empty, so let's wind it forward.
                interface_decls_it = element->interface_declaration_list.end();
            } else {
                m[(*interface_decls_it)->start_.previous_end().data().data()] = interface_t;
            }
        }
        if (struct_decls_it != element->struct_declaration_list.end()) {
            m[(*struct_decls_it)->start_.previous_end().data().data()] = struct_t;
        }
        if (table_decls_it != element->table_declaration_list.end()) {
            m[(*table_decls_it)->start_.previous_end().data().data()] = table_t;
        }
        if (union_decls_it != element->union_declaration_list.end()) {
            m[(*union_decls_it)->start_.previous_end().data().data()] = union_t;
        }
        if (xunion_decls_it != element->xunion_declaration_list.end()) {
            m[(*xunion_decls_it)->start_.previous_end().data().data()] = xunion_t;
        }
        if (m.size() == 0)
            break;

        // And the earliest top level declaration is...
        switch (m.begin()->second) {
        case bits_t:
            OnBitsDeclaration(*bits_decls_it);
            ++bits_decls_it;
            break;
        case const_t:
            OnConstDeclaration(*const_decls_it);
            ++const_decls_it;
            break;
        case enum_t:
            OnEnumDeclaration(*enum_decls_it);
            ++enum_decls_it;
            break;
        case interface_t:
            OnInterfaceDeclaration(*interface_decls_it);
            ++interface_decls_it;
            break;
        case struct_t:
            OnStructDeclaration(*struct_decls_it);
            ++struct_decls_it;
            break;
        case table_t:
            OnTableDeclaration(*table_decls_it);
            ++table_decls_it;
            break;
        case union_t:
            OnUnionDeclaration(*union_decls_it);
            ++union_decls_it;
            break;
        case xunion_t:
            OnXUnionDeclaration(*xunion_decls_it);
            ++xunion_decls_it;
            break;
        }
    }
    OnSourceElementEnd(*element);
}

void DeclarationOrderTreeVisitor::OnInterfaceDeclaration(
    std::unique_ptr<InterfaceDeclaration> const& element) {

    if (element->is_interface) {
        TreeVisitor::OnInterfaceDeclaration(element);
        return;
    }

    SourceElementMark sem(*this, *element);
    if (element->attributes != nullptr) {
        OnAttributeList(element->attributes);
    }
    OnIdentifier(element->identifier);

    auto compose_it = element->superinterfaces.begin();
    auto methods_it = element->methods.begin();

    enum Next {
        compose_t,
        method_t,
    };

    std::map<const char*, Next> m;
    for (;;) {
        // Sort in declaration order.
        m.clear();
        if (compose_it != element->superinterfaces.end()) {
            m[(*compose_it)->start_.previous_end().data().data()] = compose_t;
        }
        if (methods_it != element->methods.end()) {
            m[(*methods_it)->start_.previous_end().data().data()] = method_t;
        }
        if (m.size() == 0)
            return;

        // And the earliest declaration is...
        switch (m.begin()->second) {
        case compose_t:
            OnComposeProtocol(*compose_it);
            ++compose_it;
            break;
        case method_t:
            OnInterfaceMethod(*methods_it);
            ++methods_it;
            break;
        }
    }
}

} // namespace raw
} // namespace fidl
