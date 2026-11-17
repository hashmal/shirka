const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { test, before } = require('node:test');
const textmate = require('vscode-textmate');
const oniguruma = require('vscode-oniguruma');

const root = path.resolve(__dirname, '..');
const config = require('../language-configuration.json');
let grammar;

before(async () => {
    const wasm = fs.readFileSync(require.resolve('vscode-oniguruma/release/onig.wasm'));
    await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    const registry = new textmate.Registry({
        onigLib: Promise.resolve({
            createOnigScanner: patterns => new oniguruma.OnigScanner(patterns),
            createOnigString: text => new oniguruma.OnigString(text)
        }),
        loadGrammar: async () => require('../syntaxes/shirka.tmLanguage.json')
    });
    grammar = await registry.loadGrammarWithConfiguration('source.shirka', 1, {
        tokenTypes: { 'string.quoted.single.character.shirka': 2 }
    });
});

function tokens(source) {
    let state = textmate.INITIAL;
    return source.split('\n').flatMap(line => {
        const result = grammar.tokenizeLine(line, state);
        state = result.ruleStack;
        return result.tokens.map(token => ({
            text: line.slice(token.startIndex, token.endIndex),
            scopes: token.scopes
        }));
    });
}

function has(source, text, scope) {
    assert.ok(tokens(source).some(token => token.text === text && token.scopes.includes(scope)),
        `${JSON.stringify(source)} should scope ${JSON.stringify(text)} as ${scope}`);
}

test('binding operators and names, including prefix definitions', () => {
    has('(=> foo) [42]', 'foo', 'entity.name.function.definition.shirka');
    has('42 -> x <- x', 'x', 'entity.name.variable.binding.shirka');
    has('=>\n-- comment\nfoo', 'foo', 'entity.name.function.definition.shirka');
    has('=>foo', 'foo', 'entity.name.function.definition.shirka');
});

test('punctuation-only identifiers are operators, including user-defined names', () => {
    for (const name of ['!', '!?', '<<', '>>', '><', '+', '-', '*', '/', '%', '^',
        '=', '<=', '>=', '..', '++', '_', '$->', '$=>', '$<-', '@@', '!~', "!'"])
        has(name, name, 'keyword.operator.shirka');
    for (const name of ['foo', 'foo?', 'x!', 'a.b', '.1', '-.0', '!x', '_1', '$parse'])
        assert.ok(!tokens(name).some(token => token.scopes.includes('keyword.operator.shirka')), name);
    has('!?-- comment', '!?', 'keyword.operator.shirka');
    for (const name of [':foo', ':!', ':!?', ':<<', ':@@']) {
        has(name, name, 'constant.other.symbol.shirka');
        assert.ok(!tokens(name).some(token => token.scopes.includes('keyword.operator.shirka')));
    }
});

test('binding targets override ordinary builtin and operator classifications', () => {
    for (const arrow of ['->', '<-']) {
        for (const name of ['x', 'puts', '!?', '$parse']) {
            has(`${arrow} ${name}`, name, 'entity.name.variable.binding.shirka');
            assert.ok(!tokens(`${arrow} ${name}`).some(token => token.text === name &&
                (token.scopes.includes('support.function.shirka') || token.scopes.includes('keyword.operator.shirka'))));
        }
    }
    for (const name of ['foo', 'puts', '>>'])
        has(`(=> ${name}) [42]`, name, 'entity.name.function.definition.shirka');
    has('->\n-- comment\nx', 'x', 'entity.name.variable.binding.shirka');
    has('<-\n-- comment\nx', 'x', 'entity.name.variable.binding.shirka');
    has('=>\n-- comment\n>>', '>>', 'entity.name.function.definition.shirka');
});

test('numbers follow parser separators, not conventional numeric syntax', () => {
    for (const number of ['42', '-1', '0.', '3.14'])
        has(number, number, 'constant.numeric.shirka');
    for (const name of ['.1', '-.0', 'foo\'', '$try/failed', 'a.b', '->', '=>', '<-'])
        assert.ok(!tokens(name).some(token => token.scopes.includes('constant.numeric.shirka')));
    has('foo\'', 'foo\'', 'variable.function.shirka');
    has('1abc', '1', 'invalid.illegal.token.shirka');
});

test('symbols remain data and comments can immediately follow names', () => {
    has(':foo-- comment', ':foo', 'constant.other.symbol.shirka');
    has('foo-- comment', '--', 'comment.line.double-dash.shirka');
    has('puts-- comment', 'puts', 'support.function.shirka');
    has(':puts', ':puts', 'constant.other.symbol.shirka');
    has(':--', ':--', 'constant.other.symbol.shirka');
});

test('character punctuation never opens a list, prefix, string, or comment', () => {
    for (const literal of ["'", "'[", "']", "'(", "')", "'\"", "''", "'\\n", "'\\\\"])
        assert.ok(tokens(literal).every(token => token.scopes.includes('string.quoted.single.character.shirka')));
    has("'ab", 'b', 'variable.function.shirka');
    has("'--", '-', 'constant.character.shirka');
    const result = grammar.tokenizeLine2("'[", textmate.INITIAL).tokens;
    for (let i = 1; i < result.length; i += 2)
        assert.equal((result[i] >>> 8) & 3, 2, 'character must have VS Code string token type');
});

test('escaped and multiline strings keep bracket and comment characters inert', () => {
    assert.ok(tokens('"[ -- ( \\\" ]"').every(token => token.scopes.includes('string.quoted.double.shirka')));
    has('"a\nb" puts', 'puts', 'support.function.shirka');
    has('"\\n"', '\\n', 'constant.character.escape.shirka');
});

test('nested brackets remain distinct', () => {
    has('[[x] (puts) [y]]', '[', 'punctuation.section.brackets.begin.shirka');
    has('[[x] (puts) [y]]', ')', 'punctuation.section.parens.end.shirka');
    assert.ok(!tokens('[[x] (puts) [y]]').some(t => t.scopes.includes('invalid.illegal.token.shirka')));
});

test('only actual paired delimiters are matched or auto-closed', () => {
    assert.deepEqual(config.brackets, [['[', ']'], ['(', ')']]);
    assert.deepEqual(config.autoClosingPairs.map(pair => pair.open), ['[', '(', '"']);
    assert.ok(config.autoClosingPairs.every(pair => pair.notIn.includes('string') && pair.notIn.includes('comment')));
    assert.ok(!config.surroundingPairs.some(pair => pair[0] === "'"));
});

test('indentation ignores brackets in completed literals and comments', () => {
    const increase = new RegExp(config.indentationRules.increaseIndentPattern);
    for (const line of ['[', '[ -> x', '(each', '[ foo -- note']) assert.ok(increase.test(line), line);
    for (const line of ['[]', '(puts)', '"["', "'[", '-- [', 'foo -- [']) assert.ok(!increase.test(line), line);
    const decrease = new RegExp(config.indentationRules.decreaseIndentPattern);
    assert.ok(decrease.test('  ]'));
    assert.ok(!decrease.test("  ']"));
});

test('all repository examples, libraries, and suites tokenize without invalid tokens', () => {
    const repo = path.resolve(root, '../..');
    let files = 0;
    for (const directory of ['examples', 'lib', 'test']) {
        for (const name of fs.readdirSync(path.join(repo, directory)).filter(name => name.endsWith('.shk'))) {
            const source = fs.readFileSync(path.join(repo, directory, name), 'utf8');
            assert.ok(!tokens(source).some(token => token.scopes.includes('invalid.illegal.token.shirka')), name);
            files++;
        }
    }
    assert.ok(files >= 8);
});
