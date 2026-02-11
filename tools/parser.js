const yaml = require('js-yaml');
const fs = require('fs');
const path = require('path');

class DSLParser {
  constructor(yamlPath) {
    this.yamlPath = yamlPath;
    this.parsed = null;
  }

  parse() {
    const content = fs.readFileSync(this.yamlPath, 'utf8');
    this.parsed = yaml.load(content);
    this.validate();
    return this.parsed;
  }

  validate() {
    if (!this.parsed.service) {
      throw new Error('Missing required field: service');
    }
    if (!this.parsed.version) {
      throw new Error('Missing required field: version');
    }
    if (!this.parsed.methods || !Array.isArray(this.parsed.methods)) {
      throw new Error('Missing required field: methods');
    }

    this.parsed.methods.forEach((method, index) => {
      if (!method.name) {
        throw new Error(`Method at index ${index} is missing name`);
      }
      if (!method.request) {
        throw new Error(`Method ${method.name} is missing request definition`);
      }
      if (!method.response) {
        throw new Error(`Method ${method.name} is missing response definition`);
      }
    });
  }

  getServiceName() {
    return this.parsed.service;
  }

  getServiceVersion() {
    return this.parsed.version;
  }

  getServiceDescription() {
    return this.parsed.description || '';
  }

  getMethods() {
    return this.parsed.methods;
  }

// 生成 C 结构体名称
  toStructName(serviceName, methodName, suffix) {
    const camelService = this.toCamelCase(serviceName);
    const camelMethod = this.toCamelCase(methodName);
    return `${camelService}_${camelMethod}_${suffix}`;
  }

  // 生成 C 函数名称
  toFunctionName(serviceName, methodName, suffix) {
    const camelService = this.toCamelCase(serviceName);
    const camelMethod = this.toCamelCase(methodName);
    return `${camelService}_${camelMethod}_${suffix}`;
  }

  // 转换为驼峰命名
  toCamelCase(str) {
    return str.replace(/[-_](.)/g, (_, char) => char.toUpperCase());
  }

  // 转换为下划线命名
  toSnakeCase(str) {
    return str.replace(/[A-Z]/g, letter => `_${letter.toLowerCase()}`);
  }

  // 获取 msgpack 类型
  getMsgpackType(type) {
    const typeMap = {
      'string': 'string',
      'int': 'int64',
      'float': 'double',
      'bool': 'bool',
      'bytes': 'binary',
      'array': 'array',
      'map': 'map'
    };
    return typeMap[type] || 'string';
  }

  // 获取 C 类型
  getCType(type) {
    const typeMap = {
      'string': 'char*',
      'int': 'int64_t',
      'float': 'double',
      'bool': 'bool',
      'bytes': 'uint8_t*',
      'array': 'array',
      'map': 'map'
    };
    return typeMap[type] || 'char*';
  }
}

module.exports = DSLParser;