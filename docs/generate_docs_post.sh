# Copy all the pngs recursivly to the html folder
find . -type f -not -path "./out/*" -name "*.png" -exec cp {} ./out/html/ \;

# Replace broken code blocks in headers
sed -i "s/&lt;tt&gt;/<code>/g" out/html/md_*.html
sed -i "s/&lt;\/tt&gt;/<\/code>/g" out/html/md_*.html

# Set the current architecture map as the start page
mv ./out/html/md_architecture__r_e_a_d_m_e.html ./out/html/index.html
# Update self-links in the generated architecture index
sed -i "s/md_architecture__r_e_a_d_m_e/index/g" ./out/html/index.html

# Fix broken nav tree name in "navtreedata.js"
sed -i 's/\("[^"]*"\),\s*"index\.html"/"Project Alice", "index.html"/' ./out/html/navtreedata.js
