#!/usr/bin/env python3
"""
UVRPC DSL Code Generator with bundled FlatCC
Generates RPC server stubs and client code from FlatBuffers RPC DSL
"""

import os
import sys
import re
import shutil
import argparse
import subprocess
from pathlib import Path

try:
    from jinja2 import Environment, FileSystemLoader
except ImportError:
    print("Error: jinja2 is required. Install with: pip install jinja2")
    sys.exit(1)

# Try to find bundled flatcc
BUNDLED_FLATCC = None

def find_bundled_flatcc():
    """Find bundled flatcc executable"""
    global BUNDLED_FLATCC
    
    # Check if running from PyInstaller bundle
    if getattr(sys, 'frozen', False):
        # Running in a PyInstaller bundle
        if hasattr(sys, '_MEIPASS'):
            bundle_dir = sys._MEIPASS
            flatcc_paths = [
                os.path.join(bundle_dir, 'flatcc'),
                os.path.join(bundle_dir, 'bin', 'flatcc'),
                os.path.join(bundle_dir, 'flatcc', 'flatcc'),
            ]
            for flatcc_path in flatcc_paths:
                if os.path.exists(flatcc_path) and os.access(flatcc_path, os.X_OK):
                    BUNDLED_FLATCC = flatcc_path
                    return flatcc_path

    # Vendored build from the source tree (scripts/setup_deps.sh installs to
    # deps/flatcc/bin); resolves for both raw-script and editable installs.
    here = os.path.dirname(os.path.abspath(__file__))
    for flatcc_path in (os.path.join(here, '..', 'deps', 'flatcc', 'bin', 'flatcc'),
                        os.path.join(here, 'deps', 'flatcc', 'bin', 'flatcc')):
        if os.path.exists(flatcc_path) and os.access(flatcc_path, os.X_OK):
            BUNDLED_FLATCC = os.path.abspath(flatcc_path)
            return BUNDLED_FLATCC

    return None

class RPCParser:
    """Parse FlatBuffers RPC DSL"""
    
    def __init__(self, schema_file):
        self.schema_file = schema_file
        self.namespace = ""
        self.services = []
        self.tables = {}
        # Names are collected before any field is parsed: a table may
        # reference a table or enum declared after it, and the type resolver
        # needs the full set to tell a schema name from a primitive.
        self.table_names = set()
        self.enum_names = set()
        # Extract schema filename without extension
        self.schema_basename = Path(schema_file).stem
        
    def parse(self):
        """Parse the schema file"""
        with open(self.schema_file, 'r') as f:
            content = f.read()
        # Strip comments before anything reads the text. Each pattern below
        # scans the whole file, so a schema documenting its own syntax inside
        # a block comment -- "rpc_service ServiceName { ... }" -- used to be
        # parsed as a real service and generate code that does not compile.
        content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
        content = re.sub(r'//[^\n]*', '', content)
        # Extract namespace
        ns_match = re.search(r'namespace\s+(\w+);', content)
        if ns_match:
            self.namespace = ns_match.group(1)

        self.table_names = set(re.findall(r'table\s+(\w+)\s*\{', content))
        self.enum_names = set(re.findall(r'enum\s+(\w+)\s*:', content))
        
        # Extract tables
        table_pattern = r'table\s+(\w+)\s*\{([^}]+)\}'
        for match in re.finditer(table_pattern, content, re.DOTALL):
            table_name = match.group(1)
            fields_str = match.group(2)
            fields = self._parse_fields(fields_str)
            self.tables[table_name] = fields
        
        # Extract rpc_service definitions (standard FlatBuffers syntax)
        service_pattern = r'rpc_service\s+(\w+)\s*\{([^}]+)\}'
        for match in re.finditer(service_pattern, content, re.DOTALL):
            service_name = match.group(1)
            methods_str = match.group(2)
            methods = self._parse_methods(methods_str)
            self.services.append({
                'name': service_name,
                'methods': methods
            })
        
        return {
            'namespace': self.namespace,
            'schema_basename': self.schema_basename,
            'services': self.services,
            'tables': self.tables
        }
    
    def _parse_fields(self, fields_str):
        """Parse table fields"""
        fields = []
        for line in fields_str.split('\n'):
            line = line.strip()
            if not line or line.startswith('//'):
                continue
            # Remove inline comments (both C /* */ and C++ //)
            line = re.sub(r'/\*.*?\*/', '', line).strip()  # Remove /* ... */
            line = re.sub(r'//.*$', '', line).strip()       # Remove // ...
            if not line or not line.endswith(';'):
                continue
            # Simple field parsing
            parts = line.split(':')
            if len(parts) == 2:
                field_name = parts[0].strip()
                field_type = parts[1].strip().rstrip(';')

                # Map FlatBuffers types to C types
                type_mapping = {
                    'int32': 'int32_t',
                    'int64': 'int64_t',
                    'uint32': 'uint32_t',
                    'uint64': 'uint64_t',
                    'float': 'float',
                    'double': 'double',
                    'bool': 'bool',
                    'string': 'const char*',
                    'ubyte': 'uint8_t',
                    'byte': 'int8_t',
                }

                # Handle array types like [ubyte] or [LogEntry]
                is_array = False
                is_struct_array = False
                base = field_type
                if field_type.startswith('[') and field_type.endswith(']'):
                    is_array = True
                    base = field_type[1:-1]
                    is_struct_array = base in self.table_names

                # Resolve the schema name to the C spellings flatcc 0.6 emits.
                # Every name declared in the schema carries the namespace, and
                # the suffix tells the constructs apart: a table is {ns}_{T}
                # in builder calls and {ns}_{T}_t as a uvrpc struct member; an
                # enum is {ns}_{E}_enum_t everywhere. Primitives map straight
                # to their C type and have no schema name at all.
                ns = self.namespace
                if base in self.table_names:
                    struct_type = '{}_{}_t'.format(ns, base)
                    flatcc_type = '{}_{}'.format(ns, base)
                elif base in self.enum_names:
                    struct_type = '{}_{}_enum_t'.format(ns, base)
                    flatcc_type = struct_type
                else:
                    struct_type = type_mapping.get(base, base)
                    flatcc_type = struct_type

                fields.append({
                    'name': field_name,
                    # C type as it appears in the user-facing struct.
                    'type': struct_type,
                    # Name the .c templates pass to flatcc ({ns}_{T}_create).
                    'flatcc_type': flatcc_type,
                    # Bare schema name, for looking a table's fields up.
                    'schema_type': base,
                    'original_type': field_type,
                    'is_array': is_array,
                    'is_struct_array': is_struct_array
                })
        return fields
    
    def _parse_methods(self, methods_str):
        """Parse RPC methods"""
        methods = []
        lines = methods_str.split('\n')
        
        for i, line in enumerate(lines):
            line = line.strip()
            if not line or line.startswith('//'):
                continue
            
            # Check for stream attribute in FlatBuffers attribute syntax (stream)
            is_stream = '(stream)' in line

            # Remove FlatBuffers attributes like (stream), (oneway), etc.
            line = re.sub(r'\s*\([^)]*\)\s*;', ';', line).strip()
            
            if not line or not line.endswith(';'):
                continue
            
            # Parse: MethodName(RequestType):ReturnType;
            # Format: Add(BenchmarkAddRequest):BenchmarkAddResponse;
            match = re.match(r'(\w+)\s*\(\s*(\w+)\s*\)\s*:\s*(\w+)', line)
            if match:
                method_name = match.group(1)
                request_type = match.group(2)
                return_type = match.group(3)

                # Get request fields from tables (already processed with c_type, is_array, etc.)
                request_fields = self.tables.get(request_type, [])

                # Get response fields from tables
                response_fields = self.tables.get(return_type, [])

                # Check if this is a oneway method (EmptyResponse indicates oneway)
                is_oneway = (return_type == 'EmptyResponse')

                methods.append({
                    'name': method_name,
                    'return': return_type,
                    'request': request_type,
                    'response': return_type,
                    'request_fields': request_fields,
                    'response_fields': response_fields,
                    'is_oneway': is_oneway,
                    'is_stream': is_stream
                })
        return methods

class RPCGenerator:
    """Generate RPC code from parsed schema"""
    
    def __init__(self, output_dir, template_dir):
        self.output_dir = Path(output_dir)
        self.template_dir = Path(template_dir)
        self.env = Environment(loader=FileSystemLoader(str(self.template_dir)))
        
    def generate_server_stub(self, rpc_data):
        """Generate server stub code - one per service"""
        for service in rpc_data['services']:
            service_data = {
                'namespace': rpc_data['namespace'],
                'schema_basename': rpc_data['schema_basename'],
                'service': service,
                'service_name_lower': service['name'].lower(),
                'service_name_upper': service['name'].upper(),
            }
            
            template = self.env.get_template('server_stub.c.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_server_stub.c".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))
    
    def generate_client_code(self, rpc_data):
        """Generate client code - one per service"""
        for service in rpc_data['services']:
            service_data = {
                'namespace': rpc_data['namespace'],
                'schema_basename': rpc_data['schema_basename'],
                'service': service,
                'service_name_lower': service['name'].lower(),
                'service_name_upper': service['name'].upper(),
                'rpc_data': rpc_data,  # Pass full rpc_data for struct array serialization
            }
            
            template = self.env.get_template('client.c.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_client.c".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))
    
    def generate_header(self, rpc_data):
        """Generate header file - one per service"""
        for service in rpc_data['services']:
            service_data = {
                'namespace': rpc_data['namespace'],
                'schema_basename': rpc_data['schema_basename'],
                'service': service,
                'service_name_lower': service['name'].lower(),
                'service_name_upper': service['name'].upper(),
            }
            
            template = self.env.get_template('api.h.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_api.h".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))
    
    def generate_common_header(self, rpc_data):
        """Generate common header file - one per service"""
        for service in rpc_data['services']:
            service_data = {
                'namespace': rpc_data['namespace'],
                'schema_basename': rpc_data['schema_basename'],
                'service': service,
                'service_name_lower': service['name'].lower(),
                'service_name_upper': service['name'].upper(),
                'rpc_data': rpc_data,  # Table list, so the template can tell table types from flatcc types
            }
            
            template = self.env.get_template('rpc_common.h.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_rpc_common.h".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))
    
    def generate_common_code(self, rpc_data):
        """Generate common code file - one per service"""
        for service in rpc_data['services']:
            service_data = {
                'namespace': rpc_data['namespace'],
                'schema_basename': rpc_data['schema_basename'],
                'service': service,
                'service_name_lower': service['name'].lower(),
                'service_name_upper': service['name'].upper(),
                'rpc_data': rpc_data,  # Pass full rpc_data for struct array serialization
            }
            
            template = self.env.get_template('rpc_common.c.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_rpc_common.c".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))

    def generate_broadcast_api(self, rpc_data):
        """Generate broadcast API header - one per service"""
        for service in rpc_data['services']:
            service_data = {
                'namespace': rpc_data['namespace'],
                'schema_basename': rpc_data['schema_basename'],
                'service': service,
                'service_name_lower': service['name'].lower(),
                'service_name_upper': service['name'].upper(),
            }
            
            template = self.env.get_template('broadcast_api.h.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_api.h".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))

    def generate_broadcast_publisher(self, rpc_data):
        """Generate broadcast publisher code - one per service"""
        for service in rpc_data['services']:
            service_data = {
                'namespace': rpc_data['namespace'],
                'schema_basename': rpc_data['schema_basename'],
                'service': service,
                'service_name_lower': service['name'].lower(),
                'service_name_upper': service['name'].upper(),
            }
            
            template = self.env.get_template('broadcast_publisher.c.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_broadcast_publisher.c".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))
    
    def generate_broadcast_subscriber(self, rpc_data):
        """Generate broadcast subscriber code - one per service"""
        for service in rpc_data['services']:
            service_data = {
                'namespace': rpc_data['namespace'],
                'schema_basename': rpc_data['schema_basename'],
                'service': service,
                'service_name_lower': service['name'].lower(),
                'service_name_upper': service['name'].upper(),
            }
            
            template = self.env.get_template('broadcast_subscriber.c.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_broadcast_subscriber.c".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))

def main():
    parser = argparse.ArgumentParser(description='UVRPC DSL Code Generator')
    parser.add_argument('--flatcc', help='Path to flatcc compiler (auto-detected if not specified)')
    parser.add_argument('-o', '--output', default='generated', help='Output directory')
    parser.add_argument('-t', '--templates', default='tools/templates', help='Templates directory')
    parser.add_argument('schema', help='Schema file to process')
    
    args = parser.parse_args()

    if args.templates == parser.get_default('templates'):
        # Default is repo-layout relative; fall back to install locations so
        # the console entry point (uvrpc-gen) works outside the source tree.
        here = Path(__file__).resolve().parent
        for cand in (here / 'templates',
                     here / 'uvrpcc' / 'templates',
                     here.parent.parent / 'share' / 'uvrpcc' / 'templates'):
            if cand.is_dir():
                args.templates = str(cand)
                break
    
    # Create output directory
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Parse schema
    print("Parsing schema: {}".format(args.schema))
    rpc_parser = RPCParser(args.schema)
    rpc_data = rpc_parser.parse()
    
    print("Found {} services:".format(len(rpc_data['services'])))
    for service in rpc_data['services']:
        print("  - {}: {} methods".format(service['name'], len(service['methods'])))
    
    # Determine flatcc path
    flatcc_path = args.flatcc
    
    # Try to find bundled flatcc first
    bundled_flatcc = find_bundled_flatcc()
    if bundled_flatcc:
        print("\nUsing bundled FlatCC: {}".format(bundled_flatcc))
        flatcc_path = bundled_flatcc
    
    # Fall back to provided path or system PATH
    if not flatcc_path:
        flatcc_path = shutil.which('flatcc')
        if flatcc_path:
            print("\nUsing system FlatCC: {}".format(flatcc_path))
    
    if not flatcc_path:
        print("\nError: FlatCC not found")
        print("Please specify --flatcc or ensure FlatCC is in PATH")
        print("For bundled version, ensure flatcc was packaged correctly")
        sys.exit(1)
    
    # Generate FlatBuffers code
    print("\nGenerating FlatBuffers code with flatcc...")
    result = subprocess.run(
        [flatcc_path, '-c', '-w', '-o', args.output, args.schema],
        capture_output=True,
        text=True
    )
    
    if result.returncode != 0:
        print("FlatCC error: {}".format(result.stderr))
        return 1
    
    # Generate RPC code
    print("\nGenerating RPC code with Jinja2...")
    generator = RPCGenerator(args.output, args.templates)
    
    for service in rpc_data['services']:
        if 'Broadcast' in service['name']:
            # Broadcast mode
            generator.generate_broadcast_api(rpc_data)
            generator.generate_broadcast_publisher(rpc_data)
            generator.generate_broadcast_subscriber(rpc_data)
        else:
            # Server/Client mode
            generator.generate_header(rpc_data)
            generator.generate_common_header(rpc_data)
            generator.generate_server_stub(rpc_data)
            generator.generate_client_code(rpc_data)
            generator.generate_common_code(rpc_data)
    
    print("\nCode generation complete!")
    return 0

if __name__ == '__main__':
    import shutil
    sys.exit(main())