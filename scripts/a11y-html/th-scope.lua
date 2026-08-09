-- Pandoc filter: give every table header cell an explicit scope="col".
--
-- Pandoc emits bare <th>. Screen readers then have to guess whether a header
-- describes a column or a row; with scope="col" they reliably announce
-- "Key: Page Down, Action: cycle to the next target" when reading across a row.
-- Every table in these documents is a simple column-headed table, so col is
-- always correct.
--
-- Done as a filter rather than a regex on the output so it operates on the
-- document structure and cannot accidentally rewrite the literal text "<th>"
-- appearing inside a code sample.

function Table(tbl)
  for _, row in ipairs(tbl.head.rows) do
    for _, cell in ipairs(row.cells) do
      cell.attr.attributes['scope'] = 'col'
    end
  end
  return tbl
end
