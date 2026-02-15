"""
UVRPC DSL Code Generator
Generates RPC server stubs and client code from FlatBuffers RPC DSL
"""

import os
import sys
import re
import argparse
from pathlib import Path

try:
    from jinja2 import Environment, FileSystemLoader
except ImportError:
    print("Error: jinja2 is required. Install with: pip install jinja2")
    sys.exit(1)

class RPCParser:
    """Parse FlatBuffers RPC DSL"""
    
    def __init__(self, schema_file):
        self.schema_file = schema_file
        self.namespace = ""
        self.services = []
        self.tables = {}
        # Extract schema filename without extension
        self.schema_basename = Path(schema_file).stem
        
    def parse(self):
        """Parse the schema file"""
        with open(self.schema_file, 'r') as f:
            content = f.read()
        
        # Extract namespace
        ns_match = re.search(r'namespace\s+(\w+);', content)
        if ns_match:
            self.namespace = ns_match.group(1)
        
        # Extract tables
        table_pattern = r'table\s+(\w+)\s*\{([^}]+)\}'
        for match in re.finditer(table_pattern, content, re.DOTALL):
            table_name = match.group(1)
            fields_str = match.group(2)
            fields = self._parse_fields(fields_str)
            self.tables[table_name] = fields
        
        # Extract rpc_service definitions
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
                
                # Handle array types like [ubyte]
                is_array = False
                c_type = field_type
                if field_type.startswith('[') and field_type.endswith(']'):
                    is_array = True
                    base_type = field_type[1:-1]
                    c_type = type_mapping.get(base_type, base_type)
                else:
                    c_type = type_mapping.get(field_type, field_type)
                
                fields.append({
                    'name': field_name,
                    'type': c_type,
                    'original_type': field_type,
                    'is_array': is_array
                })
        return fields
    
    def _parse_methods(self, methods_str):
        """Parse RPC methods"""
        methods = []
        for line in methods_str.split('\n'):
            line = line.strip()
            if not line or line.startswith('//'):
                continue
            # Parse: MethodName(RequestType):ReturnType
            # Format: Add(BenchmarkAddRequest):BenchmarkAddResponse;
            match = re.match(r'(\w+)\s*\(\s*(\w+)\s*\)\s*:\s*(\w+)', line)
            if match:
                method_name = match.group(1)
                request_type = match.group(2)
                return_type = match.group(3)
                
                # Get request fields from tables
                request_fields = self.tables.get(request_type, [])
                
                methods.append({
                    'name': method_name,
                    'return': return_type,
                    'request': request_type,
                    'response': return_type,  # Add response field
                    'request_fields': request_fields
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
            }
            
            template = self.env.get_template('rpc_common.c.j2')
            output = template.render(**service_data)
            
            filename = "{}_{}_rpc_common.c".format(rpc_data['namespace'].lower(), service['name'].lower())
            output_path = self.output_dir / filename
            output_path.write_text(output)
            print("Generated: {}".format(output_path))

def main():
    parser = argparse.ArgumentParser(description='UVRPC DSL Code Generator')
    parser.add_argument('--flatcc', required=True, help='Path to flatcc compiler')
    parser.add_argument('-o', '--output', default='generated', help='Output directory')
    parser.add_argument('-t', '--templates', default='tools/templates', help='Templates directory')
    parser.add_argument('schema', help='Schema file to process')
    
    args = parser.parse_args()
    
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
    
    # Generate FlatBuffers code
    print("\nGenerating FlatBuffers code with flatcc...")
    import subprocess
    result = subprocess.run(
        [args.flatcc, '-c', '-w', '-o', args.output, args.schema],
        capture_output=True,
        text=True
    )
    
    if result.returncode != 0:
        print("FlatCC error: {}".format(result.stderr))
        return 1
    
    # Generate RPC code
    print("\nGenerating RPC code with Jinja2...")
    generator = RPCGenerator(args.output, args.templates)
    generator.generate_header(rpc_data)
    generator.generate_server_stub(rpc_data)
    generator.generate_client_code(rpc_data)
    
    print("\nCode generation complete!")
    return 0

if __name__ == '__main__':
    sys.exit(main())