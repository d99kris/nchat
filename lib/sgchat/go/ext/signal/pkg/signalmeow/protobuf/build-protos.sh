#!/bin/bash
cd $(dirname "$0")
BASE_IMPORT_PATH="go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
opts=()
for file in */*.proto; do
	opts+=("--go_opt=M${file}=${BASE_IMPORT_PATH}/$(dirname "$file")")
	opts+=("--go-grpc_opt=M${file}=${BASE_IMPORT_PATH}/$(dirname "$file")")
done
for file in org/signal/chat/*.proto; do
	file_without_ext=$(basename "$file" .proto)
	opts+=("--go_opt=M${file}=${BASE_IMPORT_PATH}/rpc/${file_without_ext}")
	opts+=("--go-grpc_opt=M${file}=${BASE_IMPORT_PATH}/rpc/${file_without_ext}")
done
protoc --go_out=. --go-grpc_out=. \
	--go_opt=module=$BASE_IMPORT_PATH \
	--go-grpc_opt=module=$BASE_IMPORT_PATH "${opts[@]}" \
	*/*.proto org/signal/chat/*.proto
pre-commit run -a
