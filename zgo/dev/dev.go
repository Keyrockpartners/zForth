// Package dev embeds dev.zgo, the ZGo device package, so the compiler can
// type-check programs that import "dev" without a source tree.
package dev

import _ "embed"

// Source is the text of dev.zgo.
//
//go:embed dev.zgo
var Source string
