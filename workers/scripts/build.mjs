import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { homedir } from 'node:os';
import { fileURLToPath } from 'node:url';
import ts from 'typescript';

const root = fileURLToPath(new URL('../../', import.meta.url));
const generated = new URL('../generated/', import.meta.url);
await mkdir(generated, { recursive: true });
const output = fileURLToPath(new URL('routes.js', generated));
execFileSync(process.env['FORGE'] ?? `${homedir()}/.forge/bin/forge`, [
  `${root}backend/src/routes.fg`, '--emit-js', '-o', output,
  '-I', `${root}vendor/forge-web`, '-I', `${root}vendor/forge-postgres`, '-I', `${root}vendor/forge-storage`, '-I', `${root}backend/src`,
], { stdio: 'inherit' });
const source = ts.createSourceFile(output, await readFile(output, 'utf8'), ts.ScriptTarget.ES2022, true, ts.ScriptKind.JS);
const functions = new Set(source.statements.filter(ts.isFunctionDeclaration).map(node => node.name?.text));
if (!functions.has('fr_dispatch')) throw new Error('Forge JS output no longer contains routes.dispatch');
const result = ts.transform(source, [context => {
  const visit = node => {
    if (ts.isPropertyAccessExpression(node) && node.expression.getText(source) === 'globalThis' && node.name.text === 'ForgeNative') {
      return context.factory.createIdentifier('native');
    }
    const child = ts.visitEachChild(node, visit, context);
    if (ts.isFunctionDeclaration(child)) {
      return context.factory.updateFunctionDeclaration(child,
        [context.factory.createModifier(ts.SyntaxKind.AsyncKeyword)], child.asteriskToken,
        child.name, child.typeParameters, child.parameters, child.type, child.body);
    }
    if (ts.isCallExpression(child) && ts.isIdentifier(child.expression) && functions.has(child.expression.text)) {
      return context.factory.createAwaitExpression(child);
    }
    return child;
  };
  return node => ts.visitNode(node, visit);
}]);
const transformed = result.transformed[0];
if (!transformed) throw new Error('Forge JS transformation produced no module');
await writeFile(new URL('program.js', generated), `export function createProgram(native) {\n${ts.createPrinter().printFile(transformed)}\nreturn { dispatch: fr_dispatch };\n}\n`);
await writeFile(new URL('program.d.ts', generated), 'export function createProgram(native: object): { dispatch(request: bigint): Promise<bigint> };\n');
result.dispose();
