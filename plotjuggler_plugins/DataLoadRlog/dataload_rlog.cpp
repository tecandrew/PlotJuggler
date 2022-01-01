#include <dataload_rlog.hpp>

QByteArray read_bz2_file(const char* fn){
  int bzError = BZ_OK;
  FILE* f = fopen(fn, "rb");
  QByteArray raw;

  BZFILE *bytes = BZ2_bzReadOpen(&bzError, f, 0, 0, NULL, 0);
  if(bzError != BZ_OK) qWarning() << "bz2 open failed";

  const size_t chunk_size = 1024*1024*64;
  size_t cur = 0;
  while (true){
    raw.resize(cur + chunk_size);

    int dled = BZ2_bzRead(&bzError, bytes, raw.data() + cur, chunk_size);
    if(bzError == BZ_STREAM_END){
      raw.resize(cur + dled);
      break;
    } else if (bzError != BZ_OK){
      qWarning() << "bz2 decompress error";
      break;
    }

    cur += chunk_size;
  }

  BZ2_bzReadClose(&bzError, bytes);
  fclose(f);

  return std::move(raw);
}

QByteArray read_raw_file(QString fn){
  auto file = QFile(fn);
  file.open(QIODevice::ReadOnly);
  return std::move(file.readAll());
}

DataLoadRlog::DataLoadRlog()
{
  _extensions.push_back("bz2");
  _extensions.push_back("rlog");
}

DataLoadRlog::~DataLoadRlog()
{
}

const std::vector<const char*>& DataLoadRlog::compatibleFileExtensions() const
{
  return _extensions;
}

bool DataLoadRlog::readDataFromFile(FileLoadInfo* fileload_info, PlotDataMapRef& plot_data)
{
  QProgressDialog progress_dialog;
  progress_dialog.setLabelText("Decompressing log...");
  progress_dialog.setWindowModality(Qt::ApplicationModal);
  progress_dialog.show();

  auto fn = fileload_info->filename;

  // Load file
  QByteArray raw;
  if (fn.endsWith(".bz2")){
    raw = read_bz2_file(fn.toStdString().c_str());
  } else {
    raw = read_raw_file(fn);
    if (raw.size() == 0) {
      qDebug() << "Raw file read failed, larger than 2GB?";
    }
  }

  kj::ArrayPtr<const capnp::word> amsg = kj::ArrayPtr((const capnp::word*)raw.data(), raw.size()/sizeof(capnp::word));

  int max_amsg_size = amsg.size();

  progress_dialog.setLabelText("Parsing log...");
  progress_dialog.setRange(0, max_amsg_size);
  progress_dialog.show();


  RlogMessageParser parser("", plot_data);

  while(amsg.size() > 0)
  {
    try
    {
      capnp::FlatArrayMessageReader cmsg = capnp::FlatArrayMessageReader(amsg);
      capnp::FlatArrayMessageReader *tmsg = new capnp::FlatArrayMessageReader(kj::ArrayPtr(amsg.begin(), cmsg.getEnd()));
      amsg = kj::ArrayPtr(cmsg.getEnd(), amsg.end());

      capnp::DynamicStruct::Reader event = tmsg->getRoot<capnp::DynamicStruct>(parser.getSchema());

      parser.parseMessageCereal(event);
    }
    catch (const kj::Exception& e)
    {
      std::cerr << e.getDescription().cStr() << std::endl;
      break;
    }

    progress_dialog.setValue(max_amsg_size - amsg.size());
    QApplication::processEvents();
    if(progress_dialog.wasCanceled())
    {
      return true;  // display what we've already parsed
    }
  }

  qDebug() << "Done reading Rlog data"; // unit tests rely on this signal
  return true;
}

bool DataLoadRlog::xmlSaveState(QDomDocument& doc, QDomElement& parent_element) const
{
  return false;
}

bool DataLoadRlog::xmlLoadState(const QDomElement& parent_element)
{
  return false;
}
