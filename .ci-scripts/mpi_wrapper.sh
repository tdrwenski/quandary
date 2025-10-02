#!/bin/bash

# Use current user's home directory instead of hardcoded /root
USER_BIN_DIR="$HOME/bin"
mkdir -p "$USER_BIN_DIR"
MPI_PATH=$(command -v mpirun)
cat > "$USER_BIN_DIR/mpirun" << EOF
#!/bin/bash
exec $MPI_PATH --map-by :OVERSUBSCRIBE --allow-run-as-root "\$@"
EOF
chmod +x "$USER_BIN_DIR/mpirun"
echo "export PATH=\"$USER_BIN_DIR:\$PATH\"" >> ~/.bashrc
