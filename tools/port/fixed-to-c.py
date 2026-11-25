#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bootstrap selected fixed-game C++ method bodies as explicit-receiver C99.

Input is a preprocessed, pinned translation unit and Clang JSON AST for that
same file. This is a porting aid, not a build dependency or a C++ compiler.
"""
import argparse
import json
from pathlib import Path
import re


def children(node):
    return [child for child in node.get('inner', []) if child.get('kind')]


class Translator:
    def __init__(self, source, tree):
        self.source, self.nodes, self.parents = source, {}, {}
        self.references = set()
        self.return_type = ''
        self.index(tree, None)

    def index(self, node, parent):
        if 'id' in node:
            self.nodes[node['id']] = node
            self.parents[node['id']] = parent
        for child in children(node):
            self.index(child, node)

    def owner(self, node):
        parent = self.nodes.get(node.get('parentDeclContextId'), self.parents.get(node.get('id')))
        names = []
        while parent and parent.get('kind') in ('CXXRecordDecl', 'RecordDecl', 'EnumDecl'):
            if parent.get('name') and parent['kind'] != 'EnumDecl':
                names.insert(0, parent['name'])
            parent = self.parents.get(parent.get('id'))
        return '_'.join(names)

    def name(self, node):
        owner = self.owner(node)
        return (owner + '_' if owner else '') + node.get('name', '')

    @staticmethod
    def type(node):
        return node.get('type', {}).get('qualType', '')

    @staticmethod
    def ctype(value):
        return value.replace('struct ', '').replace('enum ', '').replace('class ', '').replace('::', '_').replace('&', '*')

    @staticmethod
    def bounds(node):
        span = node.get('range', {})
        begin, end = span.get('begin', {}), span.get('end', {})
        if 'offset' not in begin or 'offset' not in end:
            raise ValueError('AST must describe the supplied preprocessed translation unit')
        return begin['offset'], end['offset'] + end.get('tokLen', 0)

    def text(self, node):
        begin, end = self.bounds(node)
        return self.source[begin:end]

    @staticmethod
    def unwrap(node):
        while node.get('kind') in ('ImplicitCastExpr', 'ParenExpr', 'ExprWithCleanups', 'MaterializeTemporaryExpr', 'CXXBindTemporaryExpr') and len(children(node)) == 1:
            node = children(node)[0]
        return node

    def arguments(self, values, parameters):
        result = []
        for index, value in enumerate(values):
            parameter = parameters[index] if index < len(parameters) else {}
            if value['kind'] == 'CXXDefaultArgExpr':
                defaults = children(parameter)
                if not defaults:
                    raise ValueError('default argument is absent from this declaration')
                value = defaults[-1]
            expression = self.emit(value)
            result.append('&(' + expression + ')' if '&' in self.type(parameter) else expression)
        return result

    def emit(self, node):
        kind, parts = node.get('kind'), children(node)
        if kind == 'CXXThisExpr':
            return 'self'
        if kind == 'GNUNullExpr':
            return 'NULL'
        if kind == 'CXXBoolLiteralExpr':
            return 'true' if node['value'] else 'false'
        if kind == 'DeclRefExpr':
            declaration = node.get('referencedDecl', {})
            actual = self.nodes.get(declaration.get('id'), declaration)
            name = actual.get('name', '')
            if declaration.get('id') in self.references:
                return '(*' + name + ')'
            if declaration.get('kind') in ('FunctionDecl', 'CXXMethodDecl', 'EnumConstantDecl'):
                return self.name(actual)
            return name
        if kind == 'MemberExpr':
            base = self.emit(parts[0])
            if not node.get('name'):
                return '(*' + base + ')' if node.get('isArrow') else base
            return '(' + base + ')' + ('->' if node.get('isArrow') else '.') + node['name']
        if kind in ('CallExpr', 'CXXMemberCallExpr'):
            callee = self.unwrap(parts[0])
            if kind == 'CXXMemberCallExpr':
                declaration = self.nodes[callee['referencedMemberDecl']]
                base = self.emit(children(callee)[0])
                arguments = [base if callee.get('isArrow') else '&(' + base + ')']
                function = self.name(declaration)
            else:
                declaration = self.nodes.get(callee.get('referencedDecl', {}).get('id'), {})
                arguments, function = [], self.emit(parts[0])
            parameters = [part for part in children(declaration) if part['kind'] == 'ParmVarDecl']
            arguments.extend(self.arguments(parts[1:], parameters))
            call = function + '(' + ', '.join(arguments) + ')'
            return '(*' + call + ')' if '&' in self.type(declaration).split('(')[0] else call
        if kind == 'CXXOperatorCallExpr':
            operation = self.unwrap(parts[0]).get('referencedDecl', {}).get('name', '')[8:]
            values = [self.emit(part) for part in parts[1:]]
            if operation == '=':
                return '(' + values[0] + ' = ' + values[1] + ')'
            operations = {'+': 'add', '-': 'sub', '*': 'scale', '==': 'equal', '!=': 'equal', '+=': 'add', '-=': 'sub', '*=': 'scale'}
            if operation not in operations:
                raise ValueError('unsupported overloaded operator: ' + operation)
            record = self.type(parts[1]).replace('const ', '').replace('&', '').strip()
            value = self.ctype(record) + '_' + operations[operation] + '(' + ', '.join(values) + ')'
            if operation == '!=':
                value = '!' + value
            if operation in ('+=', '-=', '*='):
                value = '(' + values[0] + ' = ' + value + ')'
            return value
        if kind in ('CXXFunctionalCastExpr', 'CXXStaticCastExpr', 'CXXReinterpretCastExpr', 'CXXConstCastExpr'):
            return '((' + self.ctype(self.type(node)) + ')(' + self.emit(parts[-1]) + '))'
        if kind == 'CXXConstructExpr':
            if not parts:
                return ''
            if len(parts) == 1 and self.type(parts[0]).replace('const ', '').replace('&', '').strip() == self.type(node):
                return self.emit(parts[0])
            return self.ctype(self.type(node)) + '_create(' + ', '.join(self.emit(part) for part in parts) + ')'
        if kind in ('VarDecl', 'ParmVarDecl'):
            name, original = node.get('name', ''), self.type(node)
            if '&' in original:
                self.references.add(node['id'])
            array = re.fullmatch(r'(.+?)(\[[^]]*\].*)', original)
            declaration = self.ctype(array[1]) + ' ' + name + array[2] if array else self.ctype(original) + ' ' + name
            if parts:
                value = self.emit(parts[-1])
                if value:
                    declaration += ' = ' + ('&(' + value + ')' if '&' in original else value)
            return declaration
        if kind == 'DeclStmt':
            return '; '.join(self.emit(part) for part in parts) + ';'
        if kind == 'ReturnStmt' and '&' in self.return_type:
            return 'return &(' + self.emit(parts[0]) + ')'
        if kind == 'BinaryOperator' and node.get('opcode') in ('+', '-', '*', '<<') and self.type(node) in ('int', 'int32') and not any('*' in self.type(part) for part in parts):
            a, b = parts
            return 'wrap_i32((uint32)(' + self.emit(a) + ') ' + node['opcode'] + ' (uint32)(' + self.emit(b) + '))'
        if kind == 'CompoundAssignOperator' and node.get('opcode') in ('+=', '-=', '*=', '<<=') and self.type(node) in ('int', 'int32'):
            a, b = parts
            pending = [a]
            while pending:
                value = pending.pop()
                if value.get('kind') in ('CallExpr', 'CXXMemberCallExpr') or value.get('opcode') in ('++', '--'):
                    raise ValueError('side-effecting compound lvalue needs a manual port')
                pending.extend(children(value))
            left = self.emit(a)
            return '(' + left + ' = wrap_i32((uint32)(' + left + ') ' + node['opcode'][:-1] + ' (uint32)(' + self.emit(b) + ')))'
        if kind.startswith('CXX') and kind not in ('CXXNullPtrLiteralExpr',):
            raise ValueError('unsupported C++ syntax: ' + kind + ' at ' + str(self.bounds(node)[0]))
        begin, end = self.bounds(node)
        replacements = []
        for part in parts:
            start, stop = self.bounds(part)
            if start < begin or stop > end:
                continue
            value = self.emit(part)
            if start == begin and stop == end:
                return value
            replacements.append((start, stop, value))
        result, position = '', begin
        for start, stop, value in sorted(replacements, key=lambda item: (item[0], -item[1])):
            if start < position:
                continue
            result += self.source[position:start] + value
            position = stop
        return result + self.source[position:end]

    def method(self, node):
        self.references = set()
        signature = self.type(node)
        self.return_type = signature.split('(')[0].strip()
        parameters = [part for part in children(node) if part['kind'] == 'ParmVarDecl']
        arguments = []
        if node['kind'] == 'CXXMethodDecl' and node.get('storageClass') != 'static':
            arguments.append(('const ' if signature.endswith(' const') else '') + self.owner(node) + ' *self')
        for parameter in parameters:
            if '&' in self.type(parameter):
                self.references.add(parameter['id'])
            arguments.append(self.ctype(self.type(parameter)) + ' ' + parameter['name'])
        body = next(part for part in children(node) if part['kind'] == 'CompoundStmt')
        return self.ctype(self.return_type) + ' ' + self.name(node) + '(' + ', '.join(arguments or ['void']) + ')\n' + self.emit(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preprocessed', required=True, type=Path)
    parser.add_argument('--ast', required=True, type=Path)
    parser.add_argument('--class', dest='record', required=True)
    parser.add_argument('--method', action='append', required=True)
    parser.add_argument('--source-ref', required=True)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    translator = Translator(args.preprocessed.read_text(), json.loads(args.ast.read_bytes()))
    selected = []
    for node in translator.nodes.values():
        if node.get('kind') != 'CXXMethodDecl' or translator.owner(node) != args.record or node.get('name') not in args.method:
            continue
        if any(part['kind'] == 'CompoundStmt' for part in children(node)):
            selected.append(node)
    if {node['name'] for node in selected} != set(args.method):
        parser.error('one or more requested method definitions were not found')
    try:
        methods = [translator.method(node) for node in selected]
    except ValueError as error:
        parser.exit(1, f'port bootstrap FAIL: {error}\n')
    header = '/* SPDX-License-Identifier: BSD-2-Clause */\n/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; ' + args.source_ref + ' at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */\n'
    args.out.write_text(header + '\n\n'.join(methods) + '\n')


if __name__ == '__main__':
    main()
