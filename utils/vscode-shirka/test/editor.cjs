// Run in an isolated VS Code Extension Development Host (see README).
const assert = require('node:assert/strict');
const vscode = require('vscode');

exports.run = async function () {
    const document = await vscode.workspace.openTextDocument({ language: 'shirka', content: '' });
    const editor = await vscode.window.showTextDocument(document);
    editor.options = { tabSize: 2, insertSpaces: true };
    const settings = vscode.workspace.getConfiguration('editor', document.uri);
    const options = { autoClosingBrackets: 'languageDefined', autoClosingQuotes: 'languageDefined', autoIndent: 'full' };
    const previous = Object.fromEntries(Object.keys(options).map(key => [key, settings.inspect(key).globalValue]));
    try {
        for (const [key, value] of Object.entries(options))
            await settings.update(key, value, vscode.ConfigurationTarget.Global);

        async function reset(text = '') {
            await editor.edit(edit => edit.replace(new vscode.Range(0, 0, document.lineCount, 0), text));
            const end = document.positionAt(document.getText().length);
            editor.selection = new vscode.Selection(end, end);
        }
        async function type(text) {
            await vscode.commands.executeCommand('type', { text });
        }
        await reset();
        await type('[');
        assert.equal(document.getText(), '[]');
        await reset();
        await type('(');
        assert.equal(document.getText(), '()');
        await reset();
        await type('"');
        assert.equal(document.getText(), '""');
        await reset();
        await type("'");
        assert.equal(document.getText(), "'");
        await type('[');
        assert.equal(document.getText(), "'[");
        await reset("'");
        await type('"');
        assert.equal(document.getText(), "'\"");
        await reset('-- ');
        await type('[');
        assert.equal(document.getText(), '-- [');
        await reset('"text ');
        await type('[');
        assert.equal(document.getText(), '"text [');
        await reset('foo');
        editor.selection = new vscode.Selection(0, 0, 0, 3);
        await type('[');
        assert.equal(document.getText(), '[foo]');
        await reset('foo');
        await vscode.commands.executeCommand('editor.action.commentLine');
        assert.equal(document.getText(), '-- foo');
        await vscode.commands.executeCommand('editor.action.commentLine');
        assert.equal(document.getText(), 'foo');
        await reset('[]');
        editor.selection = new vscode.Selection(0, 1, 0, 1);
        await type('\n');
        assert.equal(document.getText(), '[\n  \n]');
        await reset('[ [x] ]');
        editor.selection = new vscode.Selection(0, 0, 0, 0);
        await vscode.commands.executeCommand('editor.action.jumpToBracket');
        assert.equal(editor.selection.active.character, 6);
        await reset("[ '[ ]");
        editor.selection = new vscode.Selection(0, 0, 0, 0);
        await vscode.commands.executeCommand('editor.action.jumpToBracket');
        assert.equal(editor.selection.active.character, 5);
        await reset();
        console.log('Shirka editor checks passed.');
    } finally {
        for (const [key, value] of Object.entries(previous))
            await settings.update(key, value, vscode.ConfigurationTarget.Global);
    }
};
