const nunjucks = require('nunjucks');
const fs = require('fs');
const path = require('path');
const DSLParser = require('./parser');

class CodeGenerator {
  constructor(yamlPath, outputDir) {
    this.parser = new DSLParser(yamlPath);
    this.outputDir = outputDir;
    this.config = this.parser.parse();
    this.nunjucks = nunjucks.configure(path.join(__dirname, 'templates'), {
      autoescape: false,
      trimBlocks: true,
      lstripBlocks: true
    });

    // 注册自定义过滤器
    this.registerFilters();
  }

  registerFilters() {
    this.nunjucks.addFilter('upper', (str) => str.toUpperCase());
    this.nunjucks.addFilter('lower', (str) => str.toLowerCase());
    this.nunjucks.addFilter('camel', (str) => this.parser.toCamelCase(str));
    this.nunjucks.addFilter('snake', (str) => this.parser.toSnakeCase(str));
    this.nunjucks.addFilter('default', (str, defaultValue) => str || defaultValue);
  }

  generate() {
    // 确保输出目录存在
    if (!fs.existsSync(this.outputDir)) {
      fs.mkdirSync(this.outputDir, { recursive: true });
    }

    // 生成头文件
    this.generateHeader();

    // 生成源文件
    this.generateSource();

    // 生成客户端头文件
    this.generateClientHeader();

    // 生成客户端源文件
    this.generateClientSource();

    // 生成示例服务器
    this.generateExampleServer();

    // 生成示例客户端
    this.generateExampleClient();

    // 生成异步客户端示例
    this.generateAsyncClientExample();

    console.log(`Code generated successfully in ${this.outputDir}`);
  }

  generateHeader() {
    const serviceName = this.parser.getServiceName();
    const headerName = `${serviceName.toLowerCase()}_gen.h`;
    const outputPath = path.join(this.outputDir, headerName);

    const context = {
      serviceName: serviceName,
      version: this.parser.getServiceVersion(),
      description: this.parser.getServiceDescription(),
      methods: this.parser.getMethods(),
      yamlPath: this.parser.yamlPath,
      headerGuard: `${serviceName.toUpperCase()}_GEN_H`,
      headerFile: headerName,
      structName: (serviceName, methodName, suffix) => 
        this.parser.toStructName(serviceName, methodName, suffix),
      functionName: (serviceName, methodName, suffix) => 
        this.parser.toFunctionName(serviceName, methodName, suffix),
      cType: (type) => this.parser.getCType(type)
    };

    const content = this.nunjucks.render('header.njk', context);
    fs.writeFileSync(outputPath, content);
    console.log(`Generated header: ${outputPath}`);
  }

  generateSource() {
    const serviceName = this.parser.getServiceName();
    const sourceName = `${serviceName.toLowerCase()}_gen.c`;
    const outputPath = path.join(this.outputDir, sourceName);

    const context = {
      serviceName: serviceName,
      version: this.parser.getServiceVersion(),
      description: this.parser.getServiceDescription(),
      methods: this.parser.getMethods(),
      yamlPath: this.parser.yamlPath,
      headerFile: `${serviceName.toLowerCase()}_gen.h`,
      structName: (serviceName, methodName, suffix) => 
        this.parser.toStructName(serviceName, methodName, suffix),
      functionName: (serviceName, methodName, suffix) => 
        this.parser.toFunctionName(serviceName, methodName, suffix),
      cType: (type) => this.parser.getCType(type)
    };

    const content = this.nunjucks.render('source.njk', context);
    fs.writeFileSync(outputPath, content);
    console.log(`Generated source: ${outputPath}`);
  }

  generateClientHeader() {
    const serviceName = this.parser.getServiceName();
    const headerName = `${serviceName.toLowerCase()}_gen_client.h`;
    const outputPath = path.join(this.outputDir, headerName);

    const context = {
      serviceName: serviceName,
      version: this.parser.getServiceVersion(),
      description: this.parser.getServiceDescription(),
      methods: this.parser.getMethods(),
      yamlPath: this.parser.yamlPath,
      headerGuard: `${serviceName.toUpperCase()}_GEN_CLIENT_H`,
      headerFile: `${serviceName.toLowerCase()}_gen.h`,
      structName: (serviceName, methodName, suffix) =>
        this.parser.toStructName(serviceName, methodName, suffix),
      functionName: (serviceName, methodName, suffix) =>
        this.parser.toFunctionName(serviceName, methodName, suffix),
      cType: (type) => this.parser.getCType(type)
    };

    const content = this.nunjucks.render('client_header.njk', context);
    fs.writeFileSync(outputPath, content);
    console.log(`Generated client header: ${outputPath}`);
  }

  generateClientSource() {
    const serviceName = this.parser.getServiceName();
    const sourceName = `${serviceName.toLowerCase()}_gen_client.c`;
    const outputPath = path.join(this.outputDir, sourceName);

    const context = {
      serviceName: serviceName,
      version: this.parser.getServiceVersion(),
      description: this.parser.getServiceDescription(),
      methods: this.parser.getMethods(),
      yamlPath: this.parser.yamlPath,
      headerFile: `${serviceName.toLowerCase()}_gen.h`,
      structName: (serviceName, methodName, suffix) =>
        this.parser.toStructName(serviceName, methodName, suffix),
      functionName: (serviceName, methodName, suffix) =>
        this.parser.toFunctionName(serviceName, methodName, suffix),
      cType: (type) => this.parser.getCType(type)
    };

    const content = this.nunjucks.render('client_source.njk', context);
    fs.writeFileSync(outputPath, content);
    console.log(`Generated client source: ${outputPath}`);
  }

  generateExampleServer() {
    const serviceName = this.parser.getServiceName();
    const serverName = `${serviceName.toLowerCase()}_server_example.c`;
    const outputPath = path.join(this.outputDir, serverName);

    const content = this.generateServerContent();
    fs.writeFileSync(outputPath, content);
    console.log(`Generated server example: ${outputPath}`);
  }

  generateServerContent() {
    const serviceName = this.parser.getServiceName();
    const methods = this.parser.getMethods();

    let content = `/**
 * Auto-generated server example for ${serviceName}
 * Generated from ${this.parser.yamlPath}
 */

#include "../include/uvrpc.h"
#include "../src/uvrpc_internal.h"
#include "${serviceName.toLowerCase()}_gen.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mpack.h>

`;

    // 为每个方法生成处理器
    methods.forEach(method => {
      const structName = this.parser.toStructName(serviceName, method.name, 'Request');
      const responseStruct = this.parser.toStructName(serviceName, method.name, 'Response');
      const funcName = this.parser.toFunctionName(serviceName, method.name, 'Handler');

      content += `/* ${method.name} handler */
int ${funcName}(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;

    /* Deserialize request */
    ${structName}_t request;
    if (${this.parser.toFunctionName(serviceName, method.name, 'DeserializeRequest')}(request_data, request_size, &request) != 0) {
        fprintf(stderr, "Failed to deserialize ${method.name} request\\n");
        return UVRPC_ERROR;
    }

    printf("[Server] Received ${method.name} request\\n");

    /* Process request (TODO: implement your business logic here) */
    ${responseStruct}_t response;
    memset(&response, 0, sizeof(${responseStruct}_t));

`;

      // 为响应字段设置示例值
      method.response.fields.forEach(field => {
        if (field.type === 'string') {
          content += `    response.${field.name} = strdup("sample_${field.name}");\n`;
        } else if (field.type === 'int') {
          content += `    response.${field.name} = 42;\n`;
        } else if (field.type === 'float') {
          content += `    response.${field.name} = 3.14;\n`;
        } else if (field.type === 'bool') {
          content += `    response.${field.name} = true;\n`;
        }
      });

      content += `
    /* Serialize response */
    if (${this.parser.toFunctionName(serviceName, method.name, 'SerializeResponse')}(&response, response_data, response_size) != 0) {
        fprintf(stderr, "Failed to serialize ${method.name} response\\n");
        ${this.parser.toFunctionName(serviceName, method.name, 'FreeRequest')}(&request);
        ${this.parser.toFunctionName(serviceName, method.name, 'FreeResponse')}(&response);
        return UVRPC_ERROR;
    }

    printf("[Server] Sent ${method.name} response\\n");

    /* Cleanup */
    ${this.parser.toFunctionName(serviceName, method.name, 'FreeRequest')}(&request);
    ${this.parser.toFunctionName(serviceName, method.name, 'FreeResponse')}(&response);

    return UVRPC_OK;
}

`;
    });

    // 生成主函数
    content += `int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";

    printf("Starting ${serviceName} Server on %s\\n", bind_addr);

    /* Create libuv event loop */
    uv_loop_t* loop = uv_default_loop();

    /* Create ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* Create RPC server config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, bind_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 10000, 10000);

    /* Create RPC server */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\\n");
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

/* Register services */
    /* Register methods directly with full qualified names to avoid dispatcher overhead */
`;

    // 为每个方法生成注册代码
    methods.forEach(method => {
      const funcName = this.parser.toFunctionName(serviceName, method.name, 'Handler');
      const qualifiedName = `${serviceName}.${method.name}`;
      content += `    if (uvrpc_server_register_service(server, "${qualifiedName}", ${funcName}, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to register ${qualifiedName} service\\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }
`;
    });

    content += `
    /* Start server */

    printf("${serviceName} Server is running...\\n");
    printf("Press Ctrl+C to stop\\n");

    /* Run event loop */
    uv_run(loop, UV_RUN_DEFAULT);

    /* Cleanup */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);
    uv_loop_close(loop);

    printf("${serviceName} Server stopped\\n");

    return 0;
}
`;

    return content;
  }

  generateExampleClient() {
    const serviceName = this.parser.getServiceName();
    const clientName = `${serviceName.toLowerCase()}_client_example.c`;
    const outputPath = path.join(this.outputDir, clientName);

    const content = this.generateClientContent();
    fs.writeFileSync(outputPath, content);
    console.log(`Generated client example: ${outputPath}`);
  }

  generateClientContent() {
    const serviceName = this.parser.getServiceName();
    const methods = this.parser.getMethods();

    let content = `/**
 * Auto-generated client example for ${serviceName}
 * Generated from ${this.parser.yamlPath}
 */

#include "../include/uvrpc.h"
#include "${serviceName.toLowerCase()}_gen.h"
#include <stdio.h>
#include <string.h>
#include <mpack.h>

`;

    // 为每个方法生成调用函数
    methods.forEach(method => {
      const requestStruct = this.parser.toStructName(serviceName, method.name, 'Request');
      const funcName = this.parser.toFunctionName(serviceName, method.name, 'Call');

      content += `/* ${method.name} call */
void ${funcName}(uvrpc_client_t* client) {
    printf("[Client] Calling ${method.name}\\n");

    /* Create request */
    ${requestStruct}_t request;
    memset(&request, 0, sizeof(${requestStruct}_t));

`;

      // 为请求字段设置示例值
      method.request.fields.forEach(field => {
        if (field.type === 'string') {
          content += `    request.${field.name} = strdup("sample_${field.name}");\n`;
        } else if (field.type === 'int') {
          content += `    request.${field.name} = 42;\n`;
        } else if (field.type === 'float') {
          content += `    request.${field.name} = 3.14;\n`;
        } else if (field.type === 'bool') {
          content += `    request.${field.name} = true;\n`;
        }
      });

      content += `
    /* Serialize request */
    uint8_t* request_data = NULL;
    size_t request_size = 0;
    if (${this.parser.toFunctionName(serviceName, method.name, 'SerializeRequest')}(&request, &request_data, &request_size) != 0) {
        fprintf(stderr, "Failed to serialize ${method.name} request\\n");
        ${this.parser.toFunctionName(serviceName, method.name, 'FreeRequest')}(&request);
        return;
    }

    /* Call service */
    if (uvrpc_client_call(client, "${serviceName}.${method.name}", "${method.name}",
                          request_data, request_size,
                          ${this.parser.toFunctionName(serviceName, method.name, 'ResponseCallback')}, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to call ${method.name}\\n");
    }

    /* Cleanup */
    free(request_data);
    ${this.parser.toFunctionName(serviceName, method.name, 'FreeRequest')}(&request);
}

`;
    });

    // 生成响应回调
    methods.forEach(method => {
      const responseStruct = this.parser.toStructName(serviceName, method.name, 'Response');
      const funcName = this.parser.toFunctionName(serviceName, method.name, 'ResponseCallback');

      content += `/* ${method.name} response callback */
void ${funcName}(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
    (void)ctx;

    if (status != UVRPC_OK) {
        fprintf(stderr, "${method.name} call failed: %s\\n", uvrpc_strerror(status));
        return;
    }

    /* Deserialize response */
    ${responseStruct}_t response;
    if (${this.parser.toFunctionName(serviceName, method.name, 'DeserializeResponse')}(response_data, response_size, &response) != 0) {
        fprintf(stderr, "Failed to deserialize ${method.name} response\\n");
        return;
    }

    printf("[Client] Received ${method.name} response\\n");

    /* Process response (TODO: handle response data) */

    /* Cleanup */
    ${this.parser.toFunctionName(serviceName, method.name, 'FreeResponse')}(&response);
}

`;
    });

    // 生成主函数
    content += `int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";

    printf("Starting ${serviceName} Client connecting to %s\\n", server_addr);

    /* Create libuv event loop */
    uv_loop_t* loop = uv_default_loop();

    /* Create RPC config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, server_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_hwm(config, 10000, 10000);

    /* Create RPC client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\\n");
        uvrpc_config_free(config);
        return 1;
    }

    /* Connect to server */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }

    printf("${serviceName} Client connected to server\\n");

    /* Call services */
`;

    methods.forEach(method => {
      const funcName = this.parser.toFunctionName(serviceName, method.name, 'Call');
      content += `    ${funcName}(client);\n`;
    });

    content += `
    printf("${serviceName} Client sent requests, waiting for responses...\\n");

    /* Run event loop */
    uv_run(loop, UV_RUN_DEFAULT);

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(loop);

    printf("${serviceName} Client stopped\\n");

    return 0;
}
`;

    return content;
  }

  generateAsyncClientExample() {
    const serviceName = this.parser.getServiceName();
    const exampleName = `${serviceName.toLowerCase()}_async_client_example.c`;
    const outputPath = path.join(this.outputDir, exampleName);

    const context = {
      serviceName: serviceName,
      version: this.parser.getServiceVersion(),
      description: this.parser.getServiceDescription(),
      methods: this.parser.getMethods(),
      yamlPath: this.parser.yamlPath,
      structName: (serviceName, methodName, suffix) =>
        this.parser.toStructName(serviceName, methodName, suffix),
      functionName: (serviceName, methodName, suffix) =>
        this.parser.toFunctionName(serviceName, methodName, suffix),
      cType: (type) => this.parser.getCType(type)
    };

    const content = this.nunjucks.render('client_example.njk', context);
    fs.writeFileSync(outputPath, content);
    console.log(`Generated async client example: ${outputPath}`);
  }
}

module.exports = CodeGenerator;