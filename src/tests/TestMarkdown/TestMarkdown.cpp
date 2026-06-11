// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore/QString>
#include <QtTest>

#include "Markdown.h"

class TestMarkdown : public QObject {
	Q_OBJECT
private slots:
	void markdownToHTML_data();
	void markdownToHTML();
};

void TestMarkdown::markdownToHTML_data() {
	QTest::addColumn< QString >("input");
	QTest::addColumn< QString >("expectedOutput");

	QTest::newRow("plain") << "hello world"
						   << "hello world";
	QTest::newRow("soft line break joins") << "line one\nline two"
										   << "line one line two";
	QTest::newRow("paragraphs") << "para one\n\npara two"
								<< "para one<br/>para two";
	QTest::newRow("bold") << "**bold**"
						  << "<b>bold</b>";
	QTest::newRow("italic") << "*italic*"
							<< "<i>italic</i>";
	QTest::newRow("underline") << "_underline_"
							   << "<u>underline</u>";
	QTest::newRow("strikethrough") << "~~gone~~"
								   << "<s>gone</s>";
	QTest::newRow("bold italic") << "***both***"
								 << "<i><b>both</b></i>";
	QTest::newRow("inline code") << "some `code` here"
								 << "some <code>code</code> here";
	QTest::newRow("heading") << "# Title\nbody"
							 << "<h1>Title</h1>body";
	QTest::newRow("heading levels") << "### Sub"
									<< "<h3>Sub</h3>";
	QTest::newRow("block quote") << "> quoted"
								 << "<blockquote>quoted</blockquote>";
	QTest::newRow("nested block quote") << "> outer\n> > inner"
										<< "<blockquote>outer<blockquote>inner</blockquote></blockquote>";
	QTest::newRow("code block") << "```cpp\nint a;\nint b;\n```\ntail"
								<< "<pre>int a;\nint b;</pre>tail";
	QTest::newRow("unordered list") << "- one\n- two"
									<< "<ul><li>one</li><li>two</li></ul>";
	QTest::newRow("ordered list") << "1. one\n2. two"
								  << "<ol><li>one</li><li>two</li></ol>";
	QTest::newRow("nested list") << "- a\n  1. a1\n- b"
								 << "<ul><li>a</li><ol><li>a1</li></ol><li>b</li></ul>";
	QTest::newRow("explicit link") << "[text](https://example.com)"
								   << "<a href=\"https://example.com\">text</a>";
	QTest::newRow("bare link") << "see https://mumble.info now"
							   << "see <a href=\"https://mumble.info\">https://mumble.info</a> now";
	QTest::newRow("www link") << "www.example.org"
							  << "<a href=\"http://www.example.org\">www.example.org</a>";
	QTest::newRow("html is shown literally")
		<< "<b>not bold</b>"
		<< "&lt;b&gt;not bold&lt;/b&gt;";
	QTest::newRow("script is shown literally")
		<< "<script>alert(1)</script>"
		<< "&lt;script&gt;alert(1)&lt;/script&gt;";
	QTest::newRow("ampersand") << "tom & jerry"
							   << "tom &amp; jerry";
	QTest::newRow("escaped markdown") << "\\*not italic\\*"
									  << "*not italic*";
	QTest::newRow("code keeps markdown") << "`**verbatim**`"
										 << "<code>**verbatim**</code>";
}

void TestMarkdown::markdownToHTML() {
	QFETCH(QString, input);
	QFETCH(QString, expectedOutput);

	QCOMPARE(Markdown::markdownToHTML(input), expectedOutput);
}

QTEST_MAIN(TestMarkdown)
#include "TestMarkdown.moc"
