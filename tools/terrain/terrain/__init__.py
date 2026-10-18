"""Offline terrain pipeline (docs/architecture.md section 15.5).

Builds a map package (section 15.2) from open datasets. Nothing here runs in the game; the game only reads the
package. The pipeline and the C++ reader share two contracts that are tested against each other:
the ``.csr`` raster format (``terrain.csr``) and the package manifest and hash (``terrain.package``).
"""

# Written into package.json. Bump it when a change alters what a build produces.
PIPELINE_VERSION = "1.0.0"
