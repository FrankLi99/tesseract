#include "tesseract/ccstruct/imagedata.h"
#include <string>
#include <vector>

using tesseract::DocumentCache;
using tesseract::DocumentData;
using tesseract::ImageData;

namespace {

// Tests the caching mechanism of DocumentData/ImageData.

class ImagedataTest : public ::testing::Test {
 protected:
  ImagedataTest() {
  }

  // Creates a fake DocumentData, writes it to a file, and returns the filename.
  string MakeFakeDoc(int num_pages, int doc_id,
                     std::vector<string>* page_texts) {
    // The size of the fake images that we will use.
    const int kImageSize = 1048576;
    // Not using a real image here - just an array of zeros! We are just testing
    // that the truth text matches.
    std::vector<char> fake_image(kImageSize, 0);
    DocumentData write_doc("My document");
    for (int p = 0; p < num_pages; ++p) {
      // Make some fake text that is different for each page and save it.
      page_texts->push_back(
          StringPrintf("Page %d of %d in doc %d", p, num_pages, doc_id));
      // Make an imagedata and put it in the document.
      ImageData* imagedata =
          ImageData::Build("noname", p, "eng", fake_image.data(),
                           fake_image.size(), (*page_texts)[p].c_str(), NULL);
      EXPECT_EQ(kImageSize, imagedata->MemoryUsed());
      write_doc.AddPageToDocument(imagedata);
    }
    // Write it to a file.
    string filename = file::JoinPath(
        FLAGS_test_tmpdir, absl::StrCat("documentdata", doc_id, ".lstmf"));
    EXPECT_TRUE(write_doc.SaveDocument(filename.c_str(), NULL));
    return filename;
  }
};

TEST_F(ImagedataTest, CachesProperly) {
  // This test verifies that Imagedata can be stored in a DocumentData and a
  // collection of them is cached correctly given limited memory.
  // Number of pages to put in the fake document.
  const int kNumPages = 12;
  // Allowances to read the document. Big enough for 1, 3, 0, all pages.
  const int kMemoryAllowances[] = {2000000, 4000000, 1000000, 100000000, 0};
  // Order in which to read the pages, with some sequential and some seeks.
  const int kPageReadOrder[] = { 0, 1, 2, 3, 8, 4, 5, 6, 7, 11, 10, 9, -1 };

  std::vector<string> page_texts;
  string filename = MakeFakeDoc(kNumPages, 0, &page_texts);
  // Now try getting it back with different memory allowances and check that
  // the pages can still be read.
  for (int m = 0; kMemoryAllowances[m] > 0; ++m) {
    DocumentData read_doc("My document");
    EXPECT_TRUE(
        read_doc.LoadDocument(filename.c_str(), 0, kMemoryAllowances[m], NULL));
    LOG(ERROR) << "Allowance = " << kMemoryAllowances[m];
    // Read the pages in a specific order.
    for (int p = 0; kPageReadOrder[p] >= 0; ++p) {
      int page = kPageReadOrder[p];
      const ImageData* imagedata = read_doc.GetPage(page);
      EXPECT_NE(reinterpret_cast<const ImageData*>(NULL), imagedata);
      // Check that this is the right page.
      EXPECT_STREQ(page_texts[page].c_str(),
                   imagedata->transcription().string());
    }
  }
}

TEST_F(ImagedataTest, CachesMultiDocs) {
  // This test verifies that DocumentCache works to store multiple DocumentData
  // and the two caching strategies read images in the right order.
  // Number of pages in each document.
  const std::vector<int> kNumPages = {6, 5, 7};
  std::vector<std::vector<string>> page_texts;
  GenericVector<STRING> filenames;
  for (int d = 0; d < kNumPages.size(); ++d) {
    page_texts.emplace_back(std::vector<string>());
    string filename = MakeFakeDoc(kNumPages[d], d, &page_texts.back());
    filenames.push_back(STRING(filename.c_str()));
  }
  // Now try getting them back with different cache strategies and check that
  // the pages come out in the right order.
  DocumentCache robin_cache(8000000);
  robin_cache.LoadDocuments(filenames, tesseract::CS_ROUND_ROBIN, nullptr);
  DocumentCache serial_cache(8000000);
  serial_cache.LoadDocuments(filenames, tesseract::CS_SEQUENTIAL, nullptr);
  for (int p = 0; p <= 21; ++p) {
    LOG(INFO) << "Page " << p;
    const ImageData* robin_data = robin_cache.GetPageBySerial(p);
    const ImageData* serial_data = serial_cache.GetPageBySerial(p);
    CHECK(robin_data != nullptr);
    CHECK(serial_data != nullptr);
    int robin_doc = p % kNumPages.size();
    int robin_page = p / kNumPages.size() % kNumPages[robin_doc];
    // Check that this is the right page.
    EXPECT_STREQ(page_texts[robin_doc][robin_page].c_str(),
                 robin_data->transcription().string());
    int serial_doc = p / kNumPages[0] % kNumPages.size();
    int serial_page = p % kNumPages[0] % kNumPages[serial_doc];
    EXPECT_STREQ(page_texts[serial_doc][serial_page].c_str(),
                 serial_data->transcription().string());
  }
}

}  // namespace.
