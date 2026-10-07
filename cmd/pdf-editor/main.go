// SPDX-License-Identifier: GPL-3.0-only
package main

import (
	"context"
	"fmt"
	"os"
	"pdf-editor/internal/launcher"
)

func main() {
	if err := launcher.Run(context.Background(), os.Args[1:], os.Stdout, os.Stderr); err != nil {
		fmt.Fprintln(os.Stderr, "Error:", err)
		os.Exit(1)
	}
}
