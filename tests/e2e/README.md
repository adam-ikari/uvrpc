# E2E Tests

End-to-end integration tests for UVRPC.

## Running Tests

```bash
# Run all e2e tests
make test-e2e

# Run specific test
./build/dist/bin/test_e2e_tcp
```

## Test Coverage

- TCP transport communication
- Request/response flow
- Error handling
- Connection lifecycle