#!/usr/bin/env node

const { Command } = require('commander');
const path = require('path');
const fs = require('fs');
const CodeGenerator = require('./generator');

const program = new Command();

program
  .name('uvrpc-gen')
  .description('YAML DSL parser and code generator for uvrpc')
  .version('1.0.0')
  .option('-y, --yaml <path>', 'Path to YAML DSL file')
  .option('-o, --output <path>', 'Output directory for generated code', './generated')
  .parse(process.argv);

const options = program.opts();

if (!options.yaml) {
  console.error('Error: --yaml option is required');
  process.exit(1);
}

// 解析 YAML 文件路径
const yamlPath = path.resolve(options.yaml);

if (!fs.existsSync(yamlPath)) {
  console.error(`Error: YAML file not found: ${yamlPath}`);
  process.exit(1);
}

// 解析输出目录
const outputDir = path.resolve(options.output);

console.log(`Parsing YAML DSL: ${yamlPath}`);
console.log(`Output directory: ${outputDir}`);

try {
  const generator = new CodeGenerator(yamlPath, outputDir);
  generator.generate();
} catch (error) {
  console.error(`Error: ${error.message}`);
  process.exit(1);
}