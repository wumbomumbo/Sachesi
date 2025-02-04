// Copyright (C) 2014 Sacha Refshauge

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 3.0 for more details.

// A copy of the GPL 3.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official GIT repository and contact information can be found at
// http://github.com/xsacha/Sachesi

#include "rcfs.h"

#include <lzo/lzo1x.h>

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QDir>

namespace FS
{

    RCFS::RCFS(QString filename, QIODevice *file, qint64 offset, qint64 size, QString path)
        : QFileSystem(filename, file, offset, size, path, ""), _path(filename), _file(file), _offset(offset), _size(size), _name(path)
    {
        // Ensure the file is open
        if (_file && !_file->isOpen())
        {
            _file->open(QIODevice::ReadOnly);
        }
    }

    rinode RCFS::createNode(int offset)
    {
        QNXStream stream(_file);
        _file->seek(_offset + offset + 4);
        rinode ind;
        stream >> ind.mode >> ind.nameoffset >> ind.offset >> ind.size >> ind.time;
        _file->seek(_offset + ind.nameoffset);
        ind.name = QString(_file->readLine(QNX6_MAX_CHARS));
        if (ind.name == "")
            ind.name = ".";
        return ind;
    }

    QString RCFS::generateName(QString imageExt)
    {
        _file->seek(_offset + 8);
        QString board = "rcfs";
        QString variant = "unk";
        QString cpu = "unk";
        QString version = "unk";

        if (_file->readLine(4).startsWith("fs-"))
        {
            _file->seek(_offset + 0x1038);
            QNXStream stream(_file);
            READ_TMP(qint32, offset);
            rinode dotnode = createNode(offset);
            for (int i = 0; i < dotnode.size / 0x20; i++)
            {
                rinode slashdotnode = createNode(dotnode.offset + (i * 0x20));
                if (slashdotnode.name == "etc")
                {
                    for (int i = 0; i < slashdotnode.size / 0x20; i++)
                    {
                        rinode node = createNode(slashdotnode.offset + (i * 0x20));
                        if (node.name == "os.version" || node.name == "radio.version")
                        {
                            QByteArray versionData = extractFile(_offset + node.offset, node.size, node.mode);
                            version = QString(versionData).simplified();
                        }
                    }
                }
                if (slashdotnode.name.endsWith(".tdf"))
                {
                    QByteArray boardData = extractFile(_offset + slashdotnode.offset, slashdotnode.size, slashdotnode.mode);
                    foreach (QString config, QString(boardData).split('\n'))
                    {
                        if (config.startsWith("CPU="))
                        {
                            cpu = config.split('=').last().remove('"');
                        }
                        else if (config.startsWith("BOARD="))
                        {
                            board = config.split('=').last().remove('"');
                            if (board != "radio")
                                board.prepend("os.");
                        }
                        else if (config.startsWith("BOARD_CONFIG=") ||
                                 config.startsWith("RADIO_BOARD_CONFIG="))
                        {
                            variant = config.split('=').last().remove('"');
                        }
                    }
                }
            }
        }

        QString name = QString("%1.%2.%3.%4")
                           .arg(board)
                           .arg(variant)
                           .arg(version)
                           .arg(cpu);
        if (imageExt.isEmpty())
            return uniqueDir(name);

        return uniqueFile(name + imageExt);
    }

    // Returning the array of data might be dangerous if it's huge. Consider taking a var instead
    QByteArray RCFS::extractFile(qint64 node_offset, int node_size, int node_mode)
    {
        QByteArray ret;
        _file->seek(node_offset);
        QNXStream stream(_file);
        if (node_mode & QCFM_IS_LZO_COMPRESSED)
        {
            READ_TMP(int, next);
            int chunks = (next - 4) / 4;
            QList<int> sizes, offsets;
            offsets.append(next);
            for (int s = 0; s < chunks; s++)
            {
                stream >> next;
                offsets.append(next);
                sizes.append(offsets[s + 1] - offsets[s]);
            }
            char *buffer = new char[node_size];
            foreach (int size, sizes)
            {
                char *readData = new char[size];
                _file->read(readData, size);
                size_t write_len = 0x4000;
                lzo1x_decompress_safe(reinterpret_cast<const unsigned char *>(readData), size, reinterpret_cast<unsigned char *>(buffer), &write_len, nullptr);
                ret.append(buffer, write_len);
                delete[] readData;
            }
            delete[] buffer;
        }
        else
        {
            for (qint64 i = node_size; i > 0;)
            {
                QByteArray data = _file->read(qMin(BUFFER_LEN, i));
                i -= data.size();
                ret.append(data);
            }
        }
        return ret;
    }

    void RCFS::extractDir(int offset, int numNodes, QString basedir, qint64 _offset)
    {
        QNXStream stream(_file);
        QDir mainDir(basedir);
        for (int i = 0; i < numNodes; i++)
        {
            rinode node = createNode(offset + (i * 0x20));
            node.path_to = basedir;
            QString absName = node.path_to + "/" + node.name;
            if (node.mode & QCFM_IS_DIRECTORY)
            {
                mainDir.mkpath(node.name);
                if (node.size > 0)
                    extractDir(node.offset, node.size / 0x20, absName, _offset);
            }
            else
            {
                _file->seek(node.offset + _offset);
                if (node.mode & QCFM_IS_SYMLINK)
                {
#ifdef _WIN32
                    QString lnkName = absName + ".lnk";
                    QFile::link(node.path_to + "/" + _file->readLine(QNX6_MAX_CHARS), lnkName);
                    fixFileTime(lnkName, node.time);
#else
                    QFile::link(node.path_to + "/" + _file->readLine(QNX6_MAX_CHARS), absName);
#endif
                    continue;
                }
                if (node.mode & QCFM_IS_LZO_COMPRESSED)
                {
                    QFile newFile(absName);
                    newFile.open(QFile::WriteOnly);
                    READ_TMP(int, next);
                    int chunks = (next - 4) / 4;
                    QList<int> sizes, offsets;
                    offsets.append(next);
                    for (int s = 0; s < chunks; s++)
                    {
                        stream >> next;
                        offsets.append(next);
                        sizes.append(offsets[s + 1] - offsets[s]);
                    }
                    char *buffer = new char[node.size];
                    foreach (int size, sizes)
                    {
                        char *readData = new char[size];
                        _file->read(readData, size);
                        size_t write_len = 0x4000;
                        lzo1x_decompress_safe(reinterpret_cast<const unsigned char *>(readData), size, reinterpret_cast<unsigned char *>(buffer), &write_len, nullptr);
                        newFile.write(buffer, (qint64)write_len);
                        increaseCurSize(size); // Uncompressed size
                        delete[] readData;
                    }
                    delete[] buffer;
                    newFile.close();
                }
                else
                {
                    writeFile(absName, node.size, true);
                }
#ifdef _WIN32
                fixFileTime(absName, node.time);
#endif
            }
        }
    }

    bool RCFS::createContents()
    {
        _file->seek(_offset + 0x1038);
        QNXStream stream(_file);
        READ_TMP(qint32, offset);
        extractDir(offset, 1, _path, _offset);

        // Display result
        QDesktopServices::openUrl(QUrl(_path));
        return true;
    }

    bool RCFS::createImageFromFolder(const QString &folderPath, const QString &imagePath)
    {
        qDebug() << "RCFS::createImageFromFolder called with folderPath:" << folderPath << "and imagePath:" << imagePath;

        QDir dir(folderPath);
        if (!dir.exists())
        {
            qWarning() << "Folder does not exist:" << folderPath;
            return false;
        }

        QFile imageFile(imagePath);
        if (!imageFile.open(QIODevice::WriteOnly))
        {
            qWarning() << "Could not open image file for writing:" << imagePath;
            return false;
        }

        QNXStream stream(&imageFile);
        writeDirectoryContents(stream, dir, 0);

        imageFile.close();
        qDebug() << "RCFS image created successfully at" << imagePath;
        return true;
    }

    void RCFS::writeDirectoryContents(QNXStream &stream, const QDir &dir, int baseOffset)
    {
        qDebug() << "Writing directory contents for" << dir.absolutePath();
        QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        foreach (const QFileInfo &entry, entries)
        {
            if (entry.isDir())
            {
                qDebug() << "Writing directory entry for" << entry.absoluteFilePath();
                writeDirectoryEntry(stream, entry, baseOffset);
                QDir subDir(entry.absoluteFilePath());
                writeDirectoryContents(stream, subDir, baseOffset + entry.size());
            }
            else
            {
                qDebug() << "Writing file entry for" << entry.absoluteFilePath();
                writeFileEntry(stream, entry, baseOffset);
            }
        }
    }

    void RCFS::writeDirectoryEntry(QNXStream &stream, const QFileInfo &entry, int baseOffset)
    {
        qDebug() << "RCFS::writeDirectoryEntry called for" << entry.fileName();
        stream << entry.permissions();
        stream << entry.fileName().toUtf8();
        stream << baseOffset;
        stream << entry.size();
        stream << entry.lastModified().toTime_t();
    }

    void RCFS::writeFileEntry(QNXStream &stream, const QFileInfo &entry, int baseOffset)
    {
        qDebug() << "RCFS::writeFileEntry called for" << entry.fileName();
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
        {
            qWarning() << "Could not open file for reading:" << entry.absoluteFilePath();
            return;
        }

        stream << entry.permissions();
        stream << entry.fileName().toUtf8();
        stream << baseOffset;
        stream << entry.size();
        stream << entry.lastModified().toTime_t();

        QByteArray fileData = file.readAll();
        QByteArray compressedData;
        compressData(fileData, compressedData);

        stream.writeRawData(compressedData.data(), compressedData.size());

        file.close();
    }

    void RCFS::compressData(const QByteArray &input, QByteArray &output)
    {
        size_t out_len = input.size() + input.size() / 16 + 64 + 3;
        output.resize(out_len);
        void *wrkmem = malloc(LZO1X_1_MEM_COMPRESS);
        int result = lzo1x_1_compress(reinterpret_cast<const unsigned char *>(input.data()), input.size(),
                                      reinterpret_cast<unsigned char *>(output.data()), &out_len, wrkmem);
        free(wrkmem);
        if (result != LZO_E_OK)
        {
            qWarning() << "LZO compression failed with error code" << result;
            output.clear();
        }
        else
        {
            output.resize(out_len);
        }
    }

    bool RCFS::decompressRCFS(const QString &inputPath, const QString &outputPath)
    {
        qDebug() << "RCFS::decompressRCFS called with inputPath:" << inputPath << "and outputPath:" << outputPath;

        if (inputPath == outputPath)
        {
            qWarning() << "Input and output paths cannot be the same.";
            return false;
        }

        QFile inputFile(inputPath);
        if (!inputFile.open(QIODevice::ReadOnly))
        {
            qWarning() << "Could not open input file for reading:" << inputPath;
            return false;
        }

        QFile outputFile(outputPath);
        if (!outputFile.open(QIODevice::WriteOnly))
        {
            qWarning() << "Could not open output file for writing:" << outputPath;
            return false;
        }

        QByteArray header = inputFile.read(0x1038);
        if (header.size() != 0x1038)
        {
            qWarning() << "Invalid header size:" << header.size();
            return false;
        }
        outputFile.write(header);

        QNXStream inputStream(&inputFile);
        qint32 rootOffset;
        inputStream >> rootOffset;

        _file = &inputFile;
        _offset = 0;

        try
        {
            processDirectory(inputFile, outputFile, rootOffset, 1);
        }
        catch (const std::exception &e)
        {
            qWarning() << "Error during decompression:" << e.what();
            inputFile.close();
            outputFile.close();
            return false;
        }

        inputFile.close();
        outputFile.close();
        qDebug() << "RCFS decompressed successfully to" << outputPath;
        return true;
    }

    void RCFS::processDirectory(QFile &inputFile, QFile &outputFile, qint64 offset, int numNodes)
    {
        for (int i = 0; i < numNodes; i++)
        {
            // read node
            rinode node = createNode(offset + (i * 0x20));

            // put node metadata on outputted file
            qint64 currentOutputPos = outputFile.pos();
            writeNodeMetadata(outputFile, node);

            if (node.mode & QCFM_IS_DIRECTORY)
            {
                if (node.size > 0)
                {
                    processDirectory(inputFile, outputFile, node.offset, node.size / 0x20);
                }
            }
            else
            {
                inputFile.seek(node.offset);

                if (node.mode & QCFM_IS_SYMLINK)
                {
                    QByteArray linkTarget = inputFile.readLine(QNX6_MAX_CHARS);
                    outputFile.write(linkTarget);
                }
                else if (node.mode & QCFM_IS_LZO_COMPRESSED)
                {
                    processCompressedContent(inputFile, outputFile, node);
                }
                else
                {
                    copyFileContent(inputFile, outputFile, node.size);
                }
            }
        }
    }

    void RCFS::writeNodeMetadata(QFile &outputFile, const rinode &node)
    {
        QNXStream stream(&outputFile);
        stream << node.mode;
        stream << node.nameoffset;
        stream << outputFile.pos() + sizeof(qint32) * 3 + sizeof(time_t);
        stream << node.size;
        stream << node.time;
        outputFile.write(node.name.toUtf8());
        outputFile.write("\0", 1);
    }

    void RCFS::processCompressedContent(QFile &inputFile, QFile &outputFile, const rinode &node)
    {
        QNXStream inputStream(&inputFile);

        qint32 firstOffset;
        inputStream >> firstOffset;
        int chunks = (firstOffset - 4) / 4;

        QVector<qint32> chunkOffsets;
        chunkOffsets.reserve(chunks + 1);
        chunkOffsets.append(firstOffset);

        for (int i = 0; i < chunks; i++)
        {
            qint32 offset;
            inputStream >> offset;
            chunkOffsets.append(offset);
        }

        for (int i = 0; i < chunks; i++)
        {
            int chunkSize = chunkOffsets[i + 1] - chunkOffsets[i];
            QByteArray compressedData = inputFile.read(chunkSize);

            QByteArray decompressedData(0x4000, '\0');
            size_t decompressedSize = decompressedData.size();
            int result = lzo1x_decompress_safe(
                reinterpret_cast<const unsigned char *>(compressedData.constData()),
                chunkSize,
                reinterpret_cast<unsigned char *>(decompressedData.data()),
                &decompressedSize,
                nullptr);

            if (result != LZO_E_OK)
            {
                throw std::runtime_error("LZO decompression failed");
            }

            outputFile.write(decompressedData.left(decompressedSize));
        }
    }

    void RCFS::copyFileContent(QFile &inputFile, QFile &outputFile, qint64 size)
    {
        static const qint64 bufferSize = 1024 * 1024;
        char buffer[bufferSize];

        while (size > 0)
        {
            qint64 bytesToRead = qMin(bufferSize, size);
            qint64 bytesRead = inputFile.read(buffer, bytesToRead);

            if (bytesRead <= 0)
            {
                throw std::runtime_error("Failed to read file content");
            }

            if (outputFile.write(buffer, bytesRead) != bytesRead)
            {
                throw std::runtime_error("Failed to write file content");
            }

            size -= bytesRead;
        }
    }
}
